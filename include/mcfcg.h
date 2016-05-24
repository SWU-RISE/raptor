
/**
 * @file   mcfgc.h
 * @author Liyun Dai <dlyun2009@gmail.com>
 * @date   Mon Apr  4 14:36:13 2016
 *
 * @brief  column generation method for multi-commodity flow problem
 *
 *
 */
#include <vector>
#include <limits>
#include <set>
#include <map>
#include <cstring>
#include <cassert>
#include <limits>
#include <algorithm>

#include "graphalg.hpp"
using namespace fast_graph;
using namespace std;

/* DGESV prototype */
extern "C" {
void dgesv_(int* N, int* nrhs, double* a, int* lda, int* ipiv,
            double* b, int* ldb, int* info);
}

namespace mcmcf {
enum ENTER_BASE_TYPE{
  PATH_T=0,
  LINK_T=1
  
};

struct ENTER_VARIABLE
{
  ENTER_BASE_TYPE type;
  int id;
  vector<int> path;
  ENTER_VARIABLE(  ):type( PATH_T ),id( 0 ){
    
  }
};

enum EXIT_BASE_TYPE{
  DEMAND_T=0,
  STATUS_LINK=1,
  OTHER_LINK=2
  
};
struct EXIT_VARIABLE
{
  EXIT_BASE_TYPE type;
  int id;
  EXIT_VARIABLE(  ):type( DEMAND_T ), id( 0 ){
    
  }
  
};

template <typename G, typename W, typename C>
class CG {
 public:
  struct Demand {
    int src, snk;
    C bandwith;
    Demand() : src(0), snk(0), bandwith(0) {}
  };

  struct Flows {
    vector<vector<int> > paths;
    vector<C> bandwiths;
  };

 private:
  G graph;
  int origLink_num;
  G newGraph;
  W inif_weight;

  vector<Demand> demands;

  vector<W> orignal_weights;
  vector<C> orignal_caps;

  vector<W> update_weights;
  vector<C> update_caps;
  vector<C> edgeLeftBandwith;
  vector<vector<int> > paths;
  vector<int> owner;

  vector<C> primal_solution;
  map<int, C> dual_solution;

  vector<int> status_links;
  vector<int> un_status_links;

  vector<set<int> > demand_second_paths; // belong to fixed demands' second paths

  vector<set<int> > status_primary_paths; // primary path which corross conresponse status link

  vector<int> primary_paths;

  map<int, int> second_paths;
  
  vector<C> rhs;

  C CZERO;
  int K, N, J;
  W EPS;

 public:
  CG(const G& g, const vector<W>& ws, const vector<C>& caps,
     const vector<Demand>& ds)
      : graph(g), demands(ds), orignal_weights(ws), orignal_caps(caps) {
    origLink_num = graph.getLink_num();

    CZERO = ((C)1e-6);
    EPS=( ( W )1e-6 );
    
    K=demands.size(  );
    
  }

  int getJIndex(int i) const {
    assert(find(status_links.begin(  ), status_links.end(  ), i)==status_links.end(  ) );
    int re = i;
    for (vector<int>::const_iterator it = status_links.begin();
         it != status_links.end(); it++) {
      if (*it > i) re++;
    }
    return re;
  }

  bool solve() {
    if (!initial_solution()) {
      update_edge_left_bandwith();
      iteration();
    }
  }

  bool initial_solution() {
    
    paths.resize(demands.size());
    
    primal_solution.resize(demands.size());
    
    vector<bool> succ_sate(demands.size(), false);
    vector<double> temp_cap(orignal_caps);
    inif_weight = 0;
    for (int i = 0; i < origLink_num; i++) {
      inif_weight += orignal_weights[i];
    }
    inif_weight *= 2;
    inif_weight += 1;

    for (size_t i = 0; i < demands.size(); i++) {
      int src = demands[i].src;
      int snk = demands[i].snk;
      C bw = demands[i].bandwith;
      vector<W> ws = orignal_weights;
      for (size_t j = 0; j < origLink_num; j++) {
        if (temp_cap[j] < bw) {
          ws[j] = inif_weight;
        }
      }
      vector<int> path;
      if (bidijkstra_shortest_path(graph, ws, inif_weight, src, snk, path)) {
        succ_sate[i] = true;
        for (vector<int>::iterator it = path.begin(); it != path.end(); it++) {
          temp_cap[*it] -= bw;
        }
        paths[i] = path;
        primal_solution[i] = bw;
      }
    }
    vector<int> srcs, snks;
    int src, snk;
    for (int i = 0; i < origLink_num; i++) {
      graph.findSrcSnk(i, src, snk);
      srcs.push_back(src);
      snks.push_back(snk);
    }
    
    update_weights = orignal_weights;
    update_caps = orignal_caps;
    bool re = true;

    for (size_t i = 0; i < demands.size(); i++) {
      if (!succ_sate[i]) {
        re = false;
        int src = demands[i].src;
        int snk = demands[i].snk;
        C bw = demands[i].bandwith;
        srcs.push_back(src);
        snks.push_back(snk);
        update_weights.push_back(inif_weight / 2);
        update_caps.push_back(bw);
        vector<int> path;
        path.push_back(srcs.size() - 1);
        paths[i] = path;
        primal_solution[i] = bw;
      }
    }
    if (!re) {
      N=0;
      J=srcs.size(  );
      
      rhs.resize(demands.size()+srcs.size(  ), (C)0.0  );
      for( size_t i=0; i< demands.size( ); i++ ) {
        rhs[ i ]=demands[ i ].bandwith;
      }
      
      for( size_t i=0; i<update_caps.size(  ); i++  ){
        rhs[ i+K ]=update_caps[ i ];
      }
      
      newGraph.initial(srcs, snks, update_weights);
      status_links.resize(srcs.size(), -1);  // no status links
    }
    return re;
  }

  void iteration() {
    while (true) {
      /**
       *  enter variable choose
       * 
       */

      ENTER_VARIABLE enter_commodity = chooseEnterPath(); 
      if (enter_commodity.id < 0) {
        return;
      }
      EXIT_VARIABLE exit_base;

      if( PATH_T ==enter_commodity.type ){
        /**
         *  exit base  choose
         * 
         */
        exit_base = getExitBasebyPath(enter_commodity);        
      }
      else{
        exit_base = getExitBasebyLink(enter_commodity);
      }
      
      devote(enter_commodity, exit_base);

      update_edge_left_bandwith();

      update_edge_cost();
    }
  }
  /**
   *
   * choose a comodity which the current best path has biggest diff from old
   *solution
   *
   * @return
   */
  ENTER_VARIABLE chooseEnterPath() {

    W min_diff =0.0;
    vector<int> path;
    ENTER_VARIABLE  enter_variable;
    enter_variable.type=PATH_T;
    enter_variable.id=-1;
    
    for (size_t i = 0; i < demands.size(); i++) {
      
      int src = demands[i].src;
      int snk = demands[i].snk;
      
      W old_cost = path_cost(update_weights, paths[i], ((W)0.0));

      if (bidijkstra_shortest_path(newGraph, update_weights, inif_weight, src,
                                   snk, path)) {
        W new_cost = path_cost(update_weights, path, ((W)0.0));
        W temp_diff =  new_cost-old_cost;
        if (temp_diff < min_diff) {
          min_diff = temp_diff;
          
          enter_variable.id = i;
          enter_variable.path=path;
        }
      }
    }

    for(int i=0; i< J; i++  ){
      if(dual_solution[ K+N+i ]< min_diff  ){
        min_diff = dual_solution[ K+N+i ];
        enter_variable.id = i;
        enter_variable.type=LINK_T;
        enter_variable.path.clear(  );
      }
    }

    return enter_variable;
  }

  /**
   * [  I_{K*K}   A            0       ]  [ a_K ] = [ b_K ]
   * [  B         I_{N*N}      0       ]  [ a_N ] = [ b_N ]
   * [  C         D            I_{J*J} ]  [ a_J ] = [ b_J ]
   * a_N=(B b_K-b_N) /( BA-I )
   *
   * [  I_{K*K}   A            0       ]  [ y_K ] = [ d_K ]  bandwidth  envery  demand   
   * [  B         I_{N*N}      0       ]  [ y_N ] = [ c_N ]  capacity of status links
   * [  C         D            I_{J*J} ]  [ y_J ] = [ c_J ]  capacity of other links
   * y_N=( B d_K -c_N )/(BA-I)
   * @param k
   *
   * @return
   */

  EXIT_VARIABLE  getExitBasebyPath(const ENTER_VARIABLE& enterCommodity) {
    
    const vector<int> &path=enterCommodity.path;
    
    int src = demands[enterCommodity.id].src;
    int snk = demands[enterCommodity.id].snk;

    N = status_links.size();
    J= newGraph.getLink_num(  )-N;

    /**
     *  status links are  empty
     * 
     */
    if ( 0==N ){

      
    }
    else {
      int nrhs = 1;
      int lda = N;
      int* ipiv = new int[N];
      int ldb = N, info;

      /**
       * S=BA-I
       *
       */

      double* S = new double[N * N];

      fill(S, S + N * N, 0.0);
      for (int i = 0; i < N; i++) {
        S[i * N + i] = -1.0;
      }
      for (int i = 0; i < N; i++) {
        if (status_primary_paths[i].empty()) continue;

        for (int j = 0; j < N; j++) {
          int oindex = owner[K + j];
          if (status_primary_paths[i].find(oindex) !=
              status_primary_paths[i].end())
            S[i * N + j] += 1.0;
        }
      }

      /**
       * b=B b_K -b_N
       *
       */
      double* b = new double[N];

      fill(b, b + N, 0.0);
      for (vector<int>::iterator it = paths[enterCommodity.id].begin();
           it != paths[enterCommodity.id].end(); it++) {
        vector<int>::iterator fid =
            find(status_links.begin(), status_links.end(), *it);
        if (fid != status_links.end()) {
          b[fid - status_links.begin()] = 1.0;
        }
      }

      for (vector<int>::const_iterator it = path.begin(); it != path.end(); it++) {
        vector<int>::iterator fid =
            find(status_links.begin(), status_links.end(), *it);
        if (fid != status_links.end()) {
          b[fid - status_links.begin()] -= 1.0;
        }
      }
      /**
       * a_N=( Bb_K-b_N)/( BA-I )=b/S
       *
       */

      double* a_N = new double[N];

      dgesv_(&N, &nrhs, S, &lda, ipiv, b, &ldb, &info);
      if (info > 0) {
        printf(
            "The diagonal element of the triangular factor of "
            "A,\N");
        printf("U(%i,%i) is zero, so that A is singular;\N", info,
               info);
        printf("the solution could not be computed.\N");
        exit(1);
      }
      memcpy( a_N, b, N*sizeof( double )  );

      /**
       * a_K=b_K-A a_N
       *
       */

      double* a_K = new double[K];

      fill(a_K, a_K + K, 0.0);
      a_K[enterCommodity.id] = 1.0;
      for (int i = 0; i < K; i++) {
        for (set<int>::iterator it = demand_second_paths[i].begin();
             it != demand_second_paths[i].end(); it++) {
          a_K[i] -= a_N[*it - K];
        }
      }
      /**
       * a_J=b_J-C a_K-D a_N
       *
       */
      double* a_J = new double[J];
      fill(a_J, a_J + J, 0.0);
      
      for (vector<int>::const_iterator it = path.begin(); it != path.end(); it++) {
        a_J[getJIndex(*it)-N ] = 1.0;
      }
      
      vector<int> left_AK;
      for (int i = 0; i < K; i++) {
        if (a_K[i] != 0.0) {
          left_AK.push_back(i);
        }
      }

      for( size_t i=0; i< un_status_links.size(  ); i++ ){
        for (int k = 0; k < left_AK.size(); k++) {
          int pid = left_AK[k];
          if (find(paths[pid].begin(), paths[pid].end(), un_status_links[i]) !=
              paths[pid].end())
            a_J[i] -= a_K[pid];
        }        
      }


      vector<int> left_AN;

      for (int i = 0; i < N; i++) {
        if (a_N[i] != 0.0) {
          left_AN.push_back(i);
        }
      }
      

      for( size_t i=0; i< un_status_links.size(  ); i++ ){

        for (int k = 0; k < left_AN.size(); k++) {
          int pid = left_AN[k];
          if (find(paths[pid + K].begin(), paths[pid + K].end(), un_status_links[i]) !=
              paths[pid + K].end())
            a_J[i] -= a_N[pid];
        }
      }

      
      // /**
      //  *  choose enter base as i which a[ i]>0 and rhs[ i ]/a[ i ]= min rhs/a
      //  * 
      //  */

      


      int reK=-1;
      double minK=numeric_limits<double>::max(  );
      double temp;
      int i=0;
      while (i<K ) {
        if(a_K[ i ]>0  ) {
          reK=i;
          minK=rhs[ i ]/a_K[ i ];
          break;
        }
        i++;
      }
      while(i<K){
        if( a_K[ i ]>0 ){
          temp=rhs[ i ]/a_K[ i ];
          if( temp<minK ){
            reK=i;
            minK=temp;
          }
        }
      }

      int reN=-1;
      double minN=numeric_limits<double>::max(  );
      i=0;
      while (i<N ) {
        if(a_N[ i ]>0  ) {
          reN=i;
          minN= rhs[K+ status_links[i] ]/a_N[ i ];
          break;
        }
        i++;
      }

      while(i<N){
        if( a_N[ i ]>0 ){
          temp=rhs[K+ status_links[i] ]/a_N[ i ];
          if( temp<minN ){
            reN=i;
            minN=temp;
          }
        }
      }
      

      int reJ=-1;
      double minJ=numeric_limits<double>::max(  );

      i=0;
      while (i<J ) {
        if(a_J[ i ]>0  ) {
          reJ=i;
          minJ=rhs[ K+un_status_links[ i ] ]/a_J[ i ];
          break;
        }
        i++;
      }

      while(i<J){
        if( a_J[ i ]>0 ){
          temp=rhs[ K+un_status_links[ i ] ]/a_J[ i ];
          if( temp<minJ ){
            reJ=i;
            minJ=temp;
          }
        }
      }


      
      delete[] ipiv;
      delete[] S;
      delete[] b;
      delete[] a_N;
      delete[] a_K;
      delete[] a_J;

      EXIT_VARIABLE re;

      if( minK<=minN && minK<=minJ ){
        re.id=reK;
        re.type=DEMAND_T;
        return re;

      }
      if( minN<=minJ ){
        re.id=reN;
        re.type=STATUS_LINK;
        return re;
      }
      re.id=reJ;
      re.type=OTHER_LINK;
      return re;

    }
  }

  EXIT_VARIABLE  getExitBasebyLink(const ENTER_VARIABLE& enterLink) {

    EXIT_VARIABLE re;
    return re;
    
  }

  void devote(ENTER_VARIABLE& enter_commodity,  EXIT_VARIABLE& exit_base) {
    
  }

  
  
  void update_edge_left_bandwith() {
    edgeLeftBandwith = update_caps;
    size_t i = 0;
    for (i = 0; i < paths.size(); i++) {
      for (vector<int>::iterator lit = paths[i].begin(); lit != paths[i].end();
           lit++) {
        edgeLeftBandwith[*lit] -= primal_solution[i];
      }
    }
  }

  C leftBandwith(const vector<int> path) {
    if (path.empty()) return 0;
    C re = edgeLeftBandwith[path.front()];
    for (vector<int>::const_iterator it = path.begin(); it != path.end();
         it++) {
      re = min(re, edgeLeftBandwith[*it]);
    }

    return re;
  }

  void update_edge_cost() {}
};
}
