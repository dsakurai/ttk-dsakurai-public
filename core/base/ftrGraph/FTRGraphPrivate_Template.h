#ifndef FTRGRAPHPRIVATE_TEMPLATE_H
#define FTRGRAPHPRIVATE_TEMPLATE_H

#include "FTRGraph.h"

// Skeleton + propagation
#define DEBUG_1(msg) std::cout msg
// #define DEBUG_1(msg)

// Dynamic graph structure
// #define DEBUG_2(msg) std::cout msg
#define DEBUG_2(msg)

namespace ttk
{
   namespace ftr
   {
      template <typename ScalarType>
      void FTRGraph<ScalarType>::growthFromSeed(const idVertex seed, Propagation* localPropagation, const idSuperArc arcId)
      {
         DEBUG_1(<< "Start " << seed << " go up " << localPropagation->goUp() << std::endl);
#ifndef NDEBUG
         DEBUG_1(<< localPropagation->getRpz() << " " << localPropagation->print() << std::endl);
#endif

         // Check if next vertex is already visited by an arc coming here
         const bool alreadyAttached =
             checkAlreayAttached(seed, localPropagation->getNextVertex(), localPropagation);
         if (alreadyAttached)
            return;


         // skeleton
         const idNode     downNode   = graph_.makeNode(seed);
         const idSuperArc currentArc = (arcId != nullSuperArc)? arcId : graph_.openArc(downNode, localPropagation);
         graph_.visit(seed, currentArc);

#ifndef NDEBUG
         graph_.getArc(currentArc).setFromUp(localPropagation->goUp());
#endif

         // topology
         bool isJoinSadlleLast = false;
         bool isJoinSaddle = false, isSplitSaddle = false;

         // containers
         std::vector<idEdge>               lowerStarEdges, upperStarEdges;
         std::set<DynGraphNode<idVertex>*> lowerComp, upperComp;

         while (!isJoinSaddle && !isSplitSaddle && !localPropagation->empty()) {
            localPropagation->nextVertex();
            const idVertex curVert = localPropagation->getCurVertex();

            // Avoid revisiting things processed by this CC
            if (!graph_.isNode(curVert) && graph_.hasVisited(curVert, localPropagation->getRpz())) {
               DEBUG_1(<< "already seen " << curVert << std::endl);
               continue;
            }

            // Mark this vertex with the current growth
            graph_.visit(curVert, currentArc);

            DEBUG_1(<< "visit: " << curVert << std::endl);

            lowerStarEdges.clear();
            upperStarEdges.clear();
            std::tie(lowerStarEdges, upperStarEdges) = visitStar(localPropagation);

            lowerComp = lowerComps(lowerStarEdges, localPropagation);
            if(lowerComp.size() > 1){
               isJoinSaddle = true;
            }

            if (isJoinSaddle) {
               isJoinSadlleLast = checkLast(currentArc, localPropagation, lowerStarEdges);
               DEBUG_1(<< ": is join " << isJoinSadlleLast << std::endl);
               // If the current growth reaches a saddle and is not the last
               // reaching this saddle, it just stops here.
               if (!isJoinSadlleLast)
                  break;
            }

            updatePreimage(localPropagation, currentArc);

            upperComp = upperComps(upperStarEdges, localPropagation);
            if (upperComp.size() > 1) {
               DEBUG_1(<< ": is split" << std::endl);
               isSplitSaddle = true;
            }

            if (!isJoinSaddle || isSplitSaddle) {
               // add upper star for futur visit
               localGrowth(localPropagation);
            }
         }

         // if we stop, create/recover the critical point
         const idVertex upVert = localPropagation->getCurVertex();
         const idNode   upNode = updateReebGraph(currentArc, localPropagation);

         // Saddle case

         if (isJoinSadlleLast) {
            localGrowth(localPropagation);
            mergeAtSaddle(upNode, localPropagation);
         }

         if (isSplitSaddle) {
            splitAtSaddle(localPropagation);
         } else if (isJoinSadlleLast) {
            // recursive call
            const idNode     downNode = graph_.getNodeId(upVert);
            const idSuperArc newArc   = graph_.openArc(downNode, localPropagation);
            growthFromSeed(upVert, localPropagation, newArc);
         }
      }

      template <typename ScalarType>
      std::pair<std::vector<idEdge>, std::vector<idEdge>> FTRGraph<ScalarType>::visitStar(
          const Propagation* const localPropagation) const
      {
         // TODO re-use the same vectors per thread
         std::vector<idEdge> lowerStar, upperStar;

         const idEdge nbAdjEdges = mesh_->getVertexEdgeNumber(localPropagation->getCurVertex());
         lowerStar.reserve(nbAdjEdges);
         upperStar.reserve(nbAdjEdges);

         for (idEdge e = 0; e < nbAdjEdges; ++e) {
            idEdge edgeId;
            mesh_->getVertexEdge(localPropagation->getCurVertex(), e, edgeId);
            idVertex edgeLowerVert, edgeUpperVert;
            std::tie(edgeLowerVert, edgeUpperVert, std::ignore) =
                getOrderedEdge(edgeId, localPropagation);
            if (edgeLowerVert == localPropagation->getCurVertex()) {
               upperStar.emplace_back(edgeId);
            } else {
               lowerStar.emplace_back(edgeId);
            }
         }

         return {lowerStar, upperStar};
      }

      template <typename ScalarType>
      std::set<DynGraphNode<idVertex>*> FTRGraph<ScalarType>::lowerComps(
          const std::vector<idEdge>& finishingEdges, const Propagation* const localProp)
      {
         return dynGraph(localProp).findRoot(finishingEdges);
      }

      template <typename ScalarType>
      std::set<DynGraphNode<idVertex>*> FTRGraph<ScalarType>::upperComps(
          const std::vector<idEdge>& startingEdges, const Propagation* const localProp)
      {
         return dynGraph(localProp).findRoot(startingEdges);
      }

      template <typename ScalarType>
      void FTRGraph<ScalarType>::updatePreimage(const Propagation* const localPropagation,
                                                const idSuperArc         curArc)
      {
         const idCell nbAdjTriangles =
             mesh_->getVertexTriangleNumber(localPropagation->getCurVertex());

         for (idCell t = 0; t < nbAdjTriangles; ++t) {
            // Classify current cell
            idCell curTriangleid;
            mesh_->getVertexTriangle(localPropagation->getCurVertex(), t, curTriangleid);

            orderedTriangle   oTriangle  = getOrderedTriangle(curTriangleid, localPropagation);
            vertPosInTriangle curVertPos = getVertPosInTriangle(oTriangle, localPropagation);

            // std::cout << "update preimage at v" << localPropagation->getCurVertex() << " : "
            //           << printTriangle(oTriangle, localPropagation) << std::endl;

            // Update DynGraph
            // We can have an end pos on an unvisited triangle
            // in case of saddle points
            switch (curVertPos) {
               case vertPosInTriangle::Start:
                  updatePreimageStartCell(oTriangle, localPropagation, curArc);
                  break;
               case vertPosInTriangle::Middle:
                  updatePreimageMiddleCell(oTriangle, localPropagation, curArc);
                  break;
               case vertPosInTriangle::End:
                  updatePreimageEndCell(oTriangle, localPropagation, curArc);
                  break;
               default:
                  std::cout << "[FTR]: update preimage error, unknown vertPos type" << std::endl;
                  break;
            }
         }
      }

      template <typename ScalarType>
      void FTRGraph<ScalarType>::updatePreimageStartCell(const orderedTriangle&   oTriangle,
                                                         const Propagation* const localPropagation,
                                                         const idSuperArc         curArc)
      {
         const orderedEdge e0 = getOrderedEdge(std::get<0>(oTriangle), localPropagation);
         const orderedEdge e1 = getOrderedEdge(std::get<1>(oTriangle), localPropagation);
         const idVertex    w  = getWeight(e0, e1, localPropagation);
         bool t = dynGraph(localPropagation).insertEdge(std::get<0>(oTriangle), std::get<1>(oTriangle), w);

         dynGraph(localPropagation).setSubtreeArc(std::get<1>(oTriangle), curArc);

         if (t) {
            DEBUG_2(<< "start add edge: " << printEdge(std::get<0>(oTriangle), localPropagation));
            DEBUG_2(<< " :: " << printEdge(std::get<1>(oTriangle), localPropagation) << std::endl);
         } else {
            DEBUG_2(<< "start no need to create edge: " << printEdge(std::get<0>(oTriangle), localPropagation));
            DEBUG_2(<< " :: " << printEdge(std::get<1>(oTriangle), localPropagation) << std::endl);
            DEBUG_2(<< dynGraph(localPropagation).print() << std::endl);
         }
      }

      template <typename ScalarType>
      void FTRGraph<ScalarType>::updatePreimageMiddleCell(const orderedTriangle&   oTriangle,
                                                          const Propagation* const localPropagation,
                                                          const idSuperArc         curArc)
      {
         // Check if exist ?
         // If not, the triangle will be visited again once a merge have occured.
         // So we do not add the edge now
         const int t = dynGraph(localPropagation).removeEdge(std::get<0>(oTriangle), std::get<1>(oTriangle));

         if (t) {
            DEBUG_2(<< "mid replace edge: " << printEdge(std::get<0>(oTriangle), localPropagation));
            DEBUG_2(<< " :: " << printEdge(std::get<1>(oTriangle), localPropagation) << std::endl);
         }
         else {
            DEBUG_2(<< "mid no found edge: " << printEdge(std::get<0>(oTriangle), localPropagation));
            DEBUG_2(<< " :: " << printEdge(std::get<1>(oTriangle), localPropagation) << std::endl);
            DEBUG_2(<< dynGraph(localPropagation).print() << std::endl);
         }

         const orderedEdge e1 = getOrderedEdge(std::get<1>(oTriangle), localPropagation);
         const orderedEdge e2 = getOrderedEdge(std::get<2>(oTriangle), localPropagation);
         const idVertex    w  = getWeight(e1, e2, localPropagation);
         const int u = dynGraph(localPropagation).insertEdge(std::get<1>(oTriangle), std::get<2>(oTriangle), w);

         // DEBUG
         if (dynGraph(localPropagation).getSubtreeArc(std::get<2>(oTriangle)) != curArc) {
            std::cout << "FIXME "
                      << dynGraph(localPropagation).getSubtreeArc(std::get<2>(oTriangle))
                      << " != " << curArc << std::endl;
         }


         if (u) {
            DEBUG_2(<< " new edge: " << printEdge(std::get<1>(oTriangle), localPropagation));
            DEBUG_2(<< " :: " << printEdge(std::get<2>(oTriangle), localPropagation) << std::endl);
         } else {
            DEBUG_2(<< " mid no need to create edge: " << printEdge(std::get<1>(oTriangle), localPropagation));
            DEBUG_2(<< " :: " << printEdge(std::get<2>(oTriangle), localPropagation) << std::endl);
            DEBUG_2(<< dynGraph(localPropagation).print() << std::endl);
         }
      }

      template <typename ScalarType>
      void FTRGraph<ScalarType>::updatePreimageEndCell(const orderedTriangle&   oTriangle,
                                                       const Propagation* const localPropagation,
                                                       const idSuperArc         curArc)
      {
         const int t = dynGraph(localPropagation).removeEdge(std::get<1>(oTriangle), std::get<2>(oTriangle));

         if (t) {
            DEBUG_2(<< "end remove edge: " << printEdge(std::get<1>(oTriangle), localPropagation));
            DEBUG_2(<< " :: " << printEdge(std::get<2>(oTriangle), localPropagation) << std::endl);
         } else {
            DEBUG_2(<< "end not found edge: " << printEdge(std::get<1>(oTriangle), localPropagation));
            DEBUG_2(<< " :: " << printEdge(std::get<2>(oTriangle), localPropagation) << std::endl);
         }
      }

      template <typename ScalarType>
      idNode FTRGraph<ScalarType>::updateReebGraph(const idSuperArc         currentArc,
                                                   const Propagation* const localPropagation)
      {
         const idVertex upVert = localPropagation->getCurVertex(); // keep before merge
         const idNode upNode = graph_.makeNode(upVert);
         graph_.closeArc(currentArc, upNode);

         DEBUG_1(<< "close arc " << graph_.printArc(currentArc) << std::endl);

         return upNode;
      }

      template <typename ScalarType>
      void FTRGraph<ScalarType>::localGrowth(Propagation* const localPropagation)
      {
         const idVertex nbNeigh = mesh_->getVertexNeighborNumber(localPropagation->getCurVertex());
         for (idVertex n = 0; n < nbNeigh; ++n) {
            idVertex neighId;
            mesh_->getVertexNeighbor(localPropagation->getCurVertex(), n, neighId);
            if (localPropagation->compare(localPropagation->getCurVertex(), neighId)) {
               if (!toVisit_[neighId] || toVisit_[neighId]->find() != localPropagation->getRpz()) {
                  localPropagation->addNewVertex(neighId);
                  toVisit_[neighId] = localPropagation->getRpz();
               }
            }
         }
      }

      template <typename ScalarType>
      bool FTRGraph<ScalarType>::checkLast(const idSuperArc           currentArc,
                                           const Propagation* const   localPropagation,
                                           const std::vector<idEdge>& lowerStarEdges)
      {
         const idVertex   curSaddle = localPropagation->getCurVertex();
         const idVertex   nbNeigh   = mesh_->getVertexNeighborNumber(curSaddle);
         const UnionFind* rpz       = localPropagation->getRpz();

         valence decr = 0;
         for(idVertex nid = 0; nid < nbNeigh; ++nid) {
            idVertex neighId;
            mesh_->getVertexNeighbor(curSaddle, nid, neighId);

            if (localPropagation->compare(neighId, curSaddle)) {
               if (graph_.hasVisited(neighId, rpz)) {
                  DEBUG_1(<< neighId << " decrement " << curSaddle << std::endl);
                  ++decr;
               }
            }
         }

         // Check
         const idVertex nbEdgesNeigh = mesh_->getVertexEdgeNumber(curSaddle);
         for(idVertex nid = 0; nid < nbEdgesNeigh; ++nid) {
            idEdge edgeId;
            mesh_->getVertexEdge(curSaddle, nid, edgeId);

           idVertex v0;
           idVertex v1;
           mesh_->getEdgeVertex(edgeId, 0, v0);
           mesh_->getEdgeVertex(edgeId, 1, v1);

           const idVertex other = (v0 == curSaddle) ? v1 : v0;

           if (localPropagation->compare(other, curSaddle)) {
              std::cout << " | " << dynGraph(localPropagation).getSubtreeArc(edgeId) << std::endl;
           }
         }


         valence oldVal = 0;
         if (localPropagation->goUp()) {
#ifdef TTK_ENABLE_OPENMP
#pragma omp atomic capture
#endif
            {
               oldVal = graph_.valDown_[curSaddle];
               graph_.valDown_[curSaddle] -= decr;
            }

         } else {
#ifdef TTK_ENABLE_OPENMP
#pragma omp atomic capture
#endif
            {
               oldVal = graph_.valUp_[curSaddle];
               graph_.valUp_[curSaddle] -= decr;
            }
         }

         if (oldVal == -1) {
            // First task to touch this saddle, compute the valence
            idVertex totalVal = lowerStarEdges.size();
            valence  newVal   = 0;
            if (localPropagation->goUp()) {
#ifdef TTK_ENABLE_OPENMP
#pragma omp atomic capture
#endif
               {
                  newVal = graph_.valDown_[curSaddle];
                  graph_.valDown_[curSaddle] += (totalVal + 1);
               }
            } else {
#ifdef TTK_ENABLE_OPENMP
#pragma omp atomic capture
#endif
               {
                  newVal = graph_.valUp_[curSaddle];
                  graph_.valUp_[curSaddle] += (totalVal + 1);
               }
            }
            oldVal = decr + newVal + (totalVal + 1);
         }

         return oldVal == decr;
      }

      template <typename ScalarType>
      bool FTRGraph<ScalarType>::checkAlreayAttached(const idVertex saddle, const idVertex neigh, const Propagation* const localProp)
      {
         const idNode checkNode = graph_.getNodeId(saddle);
         for(const idSegmentation s : graph_.visit(neigh)) {
            if(s >= 0) {
               if (graph_.getArc(s).getUpNodeId() == checkNode) {
                  const idNode downNode = graph_.getArc(s).getDownNodeId();
                  if (downNode == nullNode) {
                     std::cout << "encounter null down node " << graph_.printArc(s) << std::endl;
                     continue;
                  }
                  if (localProp->compare(saddle, graph_.getNode(downNode).getVertexIdentifier())) {
                     return true;
                  }
               }
            }
         }
         return false;
      }

      template <typename ScalarType>
      void FTRGraph<ScalarType>::mergeAtSaddle(const idNode saddleId, Propagation* localProp)
      {
         graph_.mergeAtSaddle(saddleId);
         const idVertex saddleVert = graph_.getNode(saddleId).getVertexIdentifier();
         localProp->removeDuplicates(saddleVert);
      }

      template <typename ScalarType>
      void FTRGraph<ScalarType>::splitAtSaddle(const Propagation* const localProp)
      {
         const idVertex curVert = localProp->getCurVertex();
         const idNode   curNode = graph_.getNodeId(curVert);

         std::vector<std::tuple<idSuperArc, Propagation*>> bfsResults;
         bfsResults.reserve(4);

         const idCell nbTriNeigh = mesh_->getVertexTriangleNumber(curVert);
         for(idCell t = 0; t < nbTriNeigh; ++t) {
            idCell neighTriangle;
            mesh_->getVertexTriangle(curVert, t, neighTriangle);
            const orderedTriangle oNeighTriangle = getOrderedTriangle(neighTriangle, localProp);
            // only if curVert is not the highest point
            if (bfsCells_[neighTriangle] != curVert &&
                getVertPosInTriangle(oNeighTriangle, localProp) != vertPosInTriangle::End) {

               const idVertex endTri          = getEndVertexInTriangle(oNeighTriangle, localProp);
               const bool     alreadyAttached = checkAlreayAttached(curVert, endTri, localProp);
               if(alreadyAttached) continue;

               DEBUG_1(<< "split " << curVert << " at " << printTriangle(oNeighTriangle, localProp)) << std::endl;

               // BFS to add vertices in the current propagation for each seed
               // and its corresponfing arc
               Propagation* curProp = newPropagation(curVert, localProp->goUp()/*, localProp->getRpz()*/);
               const idSuperArc newArc = graph_.openArc(curNode, curProp);

               // fill curProp using a BFS on the current seed
               bfsPropagation(curVert, neighTriangle, curProp, newArc);
               // remove the already processed first vertex
               curProp->nextVertex();
               curProp->removeDuplicates(curVert);
               if(!curProp->empty()) {
                  bfsResults.emplace_back(std::make_tuple(newArc, curProp));
               } else {
                  graph_.getArc(newArc).hide();
               }
            }
         }

         for (auto& bfsRes : bfsResults)
            growthFromSeed(curVert, std::get<1>(bfsRes), std::get<0>(bfsRes));
      }

      /// Tools

      template <typename ScalarType>
      Propagation* FTRGraph<ScalarType>::newPropagation(const idVertex leaf, const bool fromMin,
                                                        UnionFind* rpz)
      {
         Propagation* localPropagation;
         if (fromMin) {
            auto compare_max_fun = [&](idVertex a, idVertex b) { return scalars_->isHigher(a, b); };
            localPropagation = new Propagation(leaf, compare_max_fun, fromMin, rpz);
         } else {
            auto compare_min_fun = [&](idVertex a, idVertex b) { return scalars_->isLower(a, b); };
            localPropagation = new Propagation(leaf, compare_min_fun, fromMin, rpz);
         }
         const auto   propId   = propagations_.getNext();
         propagations_[propId] = localPropagation;
         return localPropagation;
      }

      template <typename ScalarType>
      idVertex FTRGraph<ScalarType>::getWeight(const orderedEdge& e0, const orderedEdge& e1,
                                               const Propagation* const localPropagation)
      {
         const idVertex end0 = std::get<1>(e0);
         const idVertex end1 = std::get<1>(e1);

         if (localPropagation->compare(end0, end1)) {
            if (localPropagation->goDown()) {
               return -scalars_->getMirror(end0);
            }
            return scalars_->getMirror(end0);
         }

         if (localPropagation->goDown()) {
            return -scalars_->getMirror(end1);
         }
         return scalars_->getMirror(end1);
      }

      template <typename ScalarType>
      orderedEdge FTRGraph<ScalarType>::getOrderedEdge(const idEdge             edgeId,
                                                       const Propagation* const localPropagation) const
      {
         idVertex edge0Vert, edge1Vert;
         mesh_->getEdgeVertex(edgeId, 0, edge0Vert);
         mesh_->getEdgeVertex(edgeId, 1, edge1Vert);

         if (localPropagation->compare(edge0Vert, edge1Vert)) {
            return std::make_tuple(edge0Vert, edge1Vert, edgeId);
         } else {
            return std::make_tuple(edge1Vert, edge0Vert, edgeId);
         }
      }

      template <typename ScalarType>
      orderedTriangle FTRGraph<ScalarType>::getOrderedTriangle(
          const idCell cellId, const Propagation* const localPropagation) const
      {
         idEdge edges[3];
         mesh_->getTriangleEdge(cellId, 0, edges[0]);
         mesh_->getTriangleEdge(cellId, 1, edges[1]);
         mesh_->getTriangleEdge(cellId, 2, edges[2]);

         orderedEdge oEdges[3];
         oEdges[0] = getOrderedEdge(edges[0], localPropagation);
         oEdges[1] = getOrderedEdge(edges[1], localPropagation);
         oEdges[2] = getOrderedEdge(edges[2], localPropagation);

         auto compareOEdges = [localPropagation](const orderedEdge& a, const orderedEdge& b) {
            return localPropagation->compare(std::get<0>(a), std::get<0>(b)) ||
                   (std::get<0>(a) == std::get<0>(b) &&
                    localPropagation->compare(std::get<1>(a), std::get<1>(b)));
         };

         if (compareOEdges(oEdges[0], oEdges[1])) {
            // 1 2 3
            // 1 3 2
            // 2 3 1

            if (compareOEdges(oEdges[1], oEdges[2])) {
               // 1 2 3
               return std::make_tuple(std::get<2>(oEdges[0]), std::get<2>(oEdges[1]), std::get<2>(oEdges[2]), cellId);
            }
            // 1 3 2
            // 2 3 1

            if (compareOEdges(oEdges[0], oEdges[2])) {
               // 1 3 2
               return std::make_tuple(std::get<2>(oEdges[0]), std::get<2>(oEdges[2]), std::get<2>(oEdges[1]), cellId);
            }

            // 2 3 1
            return std::make_tuple(std::get<2>(oEdges[2]), std::get<2>(oEdges[0]), std::get<2>(oEdges[1]), cellId);
         }

         // 2 1 3
         // 3 2 1
         // 3 1 2

         if(compareOEdges(oEdges[0], oEdges[2])) {
            // 2 1 3
            return std::make_tuple(std::get<2>(oEdges[1]), std::get<2>(oEdges[0]), std::get<2>(oEdges[2]), cellId);
         }

         // 3 2 1
         // 3 1 2

         if(compareOEdges(oEdges[1], oEdges[2])) {
            // 3 1 2
            return std::make_tuple(std::get<2>(oEdges[1]), std::get<2>(oEdges[2]), std::get<2>(oEdges[0]), cellId);
         }

         // 3 2 1
         return std::make_tuple(std::get<2>(oEdges[2]), std::get<2>(oEdges[1]), std::get<2>(oEdges[0]), cellId);
      }

      template <typename ScalarType>
      vertPosInTriangle FTRGraph<ScalarType>::getVertPosInTriangle(
          const orderedTriangle& oTriangle, const Propagation* const localPropagation) const
      {
         orderedEdge firstEdge = getOrderedEdge(std::get<0>(oTriangle), localPropagation);
         if (std::get<0>(firstEdge) == localPropagation->getCurVertex()) {
            return vertPosInTriangle::Start;
         } else if (std::get<1>(firstEdge) == localPropagation->getCurVertex()) {
            return vertPosInTriangle::Middle;
         } else {
            return vertPosInTriangle::End;
         }
      }

      template <typename ScalarType>
      idVertex FTRGraph<ScalarType>::getEndVertexInTriangle(
          const orderedTriangle& oTriangle, const Propagation* const localPropagation) const
      {
         const orderedEdge& higherEdge = getOrderedEdge(std::get<1>(oTriangle), localPropagation);
         return std::get<1>(higherEdge);
      }
   }
}

#endif /* end of include guard: FTRGRAPHPRIVATE_TEMPLATE_H */
