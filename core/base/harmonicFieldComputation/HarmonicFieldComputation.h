/// \ingroup base
/// \class ttk::HarmonicFieldComputation
/// \author Pierre Guillou <pierre.guillou@lip6.fr>
/// \author Julien Tierny <julien.tierny@lip6.fr>
/// \date February 2019
///
/// \brief TTK processing package for the topological simplification of scalar
/// data.
///
///
///
/// \sa ttkHarmonicFieldComputation.cpp % for a usage example.

#pragma once

// base code includes
#include <Geometry.h>
#include <Triangulation.h>
#include <Wrapper.h>
#include <cmath>
#include <set>
#include <tuple>
#include <type_traits>

#ifdef TTK_ENABLE_EIGEN
#include <Eigen/Sparse>
#endif // TTK_ENABLE_EIGEN

namespace ttk {

class HarmonicFieldComputation : public Debug {

public:
  HarmonicFieldComputation();

  // default destructor
  ~HarmonicFieldComputation() override = default;
  // default copy constructor
  HarmonicFieldComputation(const HarmonicFieldComputation &) = default;
  // default move constructor
  HarmonicFieldComputation(HarmonicFieldComputation &&) = default;
  // default copy assignment operator
  HarmonicFieldComputation &
  operator=(const HarmonicFieldComputation &) = default;
  // default move assignment operator
  HarmonicFieldComputation &operator=(HarmonicFieldComputation &&) = default;

  inline int setVertexNumber(SimplexId vertexNumber) {
    vertexNumber_ = vertexNumber;
    return 0;
  }
  inline int setConstraintNumber(SimplexId constraintNumber) {
    constraintNumber_ = constraintNumber;
    return 0;
  }
  inline int setUseCotanMethod(bool useCotanMethod) {
    useCotanMethod_ = useCotanMethod;
    return 0;
  }
  inline int setupTriangulation(Triangulation *triangulation) {
    triangulation_ = triangulation;
    if (triangulation_ != nullptr) {
      vertexNumber_ = triangulation_->getNumberOfVertices();
      triangulation_->preprocessVertexNeighbors();
    }
    return 0;
  }
  inline int setSources(void *data) {
    sources_ = data;
    return 0;
  }
  inline int setConstraints(void *data) {
    constraints_ = data;
    return 0;
  }
  inline int setOutputScalarFieldPointer(void *data) {
    outputScalarFieldPointer_ = data;
    return 0;
  }

  inline int setOutputIdentifiers(void *data) {
    outputIdentifiers_ = data;
    return 0;
  }

  inline int setOutputSegmentation(void *data) {
    outputSegmentation_ = data;
    return 0;
  }

  template <typename scalarFieldType> int execute() const;

  template <typename SparseMatrixType, typename TripletsType>
  SparseMatrixType compute_laplacian() const;

  template <typename SparseMatrixType, typename TripletsType>
  SparseMatrixType compute_laplacian_with_cotan_weights() const;

protected:
  // number of vertices in the mesh
  SimplexId vertexNumber_;
  // number of constraints
  SimplexId constraintNumber_;
  // cotan weights vs simple laplacian resolution
  bool useCotanMethod_;
  // the mesh
  Triangulation *triangulation_;
  // array of mesh points with scalar constraints
  // should be of constraintNumber_ size
  void *sources_;
  // array of scalar constraints on sources_
  // should be of constraintNumber_ size
  void *constraints_;
  // output of harmonic field computation
  void *outputScalarFieldPointer_;
  // ??
  void *outputIdentifiers_;
  // ??
  void *outputSegmentation_;
};
} // namespace ttk

#ifdef TTK_ENABLE_EIGEN
using SpMat = Eigen::SparseMatrix<double>;
using SpVec = Eigen::SparseVector<double>;
using Tri = Eigen::Triplet<double>;
#endif // TTK_ENABLE_EIGEN

template <typename SparseMatrixType, typename TripletType>
SparseMatrixType ttk::HarmonicFieldComputation::compute_laplacian() const {
  SparseMatrixType lap(vertexNumber_, vertexNumber_);
  std::vector<TripletType> triplets;
#define USE_SYMMETRIC_LAPLACIAN
#ifdef USE_SYMMETRIC_LAPLACIAN
  for (SimplexId i = 0; i < vertexNumber_; ++i) {
    SimplexId nneigh = triangulation_->getVertexNeighborNumber(SimplexId(i));
    triplets.emplace_back(TripletType(i, i, double(nneigh)));
    // rest: neighbors mapping
    for (SimplexId j = 0; j < nneigh; ++j) {
      SimplexId neighid = -1;
      triangulation_->getVertexNeighbor(i, j, neighid);
      triplets.emplace_back(TripletType(i, neighid, -1.0));
    }
  }
#else
  for (SimplexId i = 0; i < vertexNumber_; ++j) {
    triplets.emplace_back(TripletType(i, i, 1.0));
    SimplexId nneigh = triangulation_->getVertexNeighborNumber(SimplexId(i));
    // rest: neighbors mapping
    for (SimplexId j = 0; j < nneigh; ++j) {
      SimplexId neighid = -1;
      triangulation_->getVertexNeighbor(i, j, neighid);
      triplets.emplace_back(TripletType(i, j, -1.0 / double(nneigh)));
    }
  }
#endif // USE_SYMMETRIC_LAPLACIAN
  lap.setFromTriplets(triplets.begin(), triplets.end());
  return lap;
}

template <typename SparseMatrixType, typename TripletType>
SparseMatrixType
ttk::HarmonicFieldComputation::compute_laplacian_with_cotan_weights() const {
  std::stringstream msg;
  using std::cout;
  using std::endl;

  msg << "[HarmonicFieldComputation] Beginning graph laplacian computation"
      << endl;
  dMsg(cout, msg.str(), advancedInfoMsg);

  SparseMatrixType lap(vertexNumber_, vertexNumber_);
  std::vector<TripletType> triplets;

  // symmetric laplacian version
  for (SimplexId i = 0; i < vertexNumber_; ++i) {
    SimplexId nneigh = triangulation_->getVertexNeighborNumber(SimplexId(i));
    triplets.emplace_back(TripletType(i, i, double(nneigh)));
  }
  lap.setFromTriplets(triplets.begin(), triplets.end());

  // rest: cotan weights
  for (SimplexId i = 0; i < vertexNumber_; ++i) {
    SimplexId nedges = triangulation_->getVertexEdgeNumber(i);
    for (SimplexId j = 0; j < nedges; ++j) {
      SimplexId edge;
      triangulation_->getVertexEdge(i, j, edge);
      SimplexId neigh;
      // get the neighbor corresponding to the current edge
      for (SimplexId k = 0; k < 2; k++) {
        triangulation_->getEdgeVertex(edge, k, neigh);
        if (neigh != i) {
          break;
        }
      }

      // skip computation if already done
      if (lap.coeffReff(i, neigh) != 0) {
        break;
      }

      SimplexId ntriangles = triangulation_->getEdgeTriangleNumber(edge);
      std::vector<double> cotans(ntriangles);
      for (SimplexId k = 0; k < ntriangles; k++) {
        SimplexId triangle;
        triangulation_->getEdgeTriangle(edge, k, triangle);
        SimplexId thirdNeigh;
        // assume a triangle has only 3 vertices
        // TODO 3D case
        for (SimplexId l = 0; l < 3; l++) {
          triangulation_->getTriangleEdge(triangle, l, thirdNeigh);
          if (thirdNeigh != i && thirdNeigh != neigh) {
            break;
          }
        }
        float coordsf[9];
        triangulation_->getVertexPoint(i, coordsf[0], coordsf[1], coordsf[2]);
        triangulation_->getVertexPoint(neigh, coordsf[3], coordsf[4],
                                       coordsf[5]);
        triangulation_->getVertexPoint(thirdNeigh, coordsf[6], coordsf[7],
                                       coordsf[8]);
        double coords[9];
        for (SimplexId k = 0; k < 9; k++) {
          coords[k] = double(coordsf[k]);
        }
        double angle = ttk::Geometry::angle(&coords[6],  // thirdNeigh
                                            &coords[0],  // i
                                            &coords[6],  // thirdNeigh
                                            &coords[3]); // neigh
        cotans.emplace_back(std::tan(1.0 / angle));
      }

      // fill laplacian matrix also for neighbor
      lap.coeffRef(i, neigh) = .5 * (cotans[0] + cotans[1]);
      lap.coeffRef(neigh, i) = .5 * (cotans[0] + cotans[1]);
    }
  }
  msg << "[HarmonicFieldComputation] Graph laplacian computed" << endl;
  dMsg(cout, msg.str(), advancedInfoMsg);
  return lap;
}

// if the package is a pure template typename, uncomment the following line
// #include                  <HarmonicFieldComputation.cpp>

// main routine
template <typename scalarFieldType>
int ttk::HarmonicFieldComputation::execute() const {

  using std::cout;
  using std::endl;
  using std::stringstream;

  // scalar field constraints vertices
  auto *identifiers = static_cast<SimplexId *>(sources_);
  // scalar field: 0 everywhere except on constraint vertices
  auto *sf = static_cast<scalarFieldType *>(constraints_);

  Timer t;
  stringstream msg;

  {
    msg << "[HarmonicFieldComputation] Beginnning computation" << endl;
    dMsg(cout, msg.str(), advancedInfoMsg);
  }

  // get unique constraint vertices
  std::set<SimplexId> identifiersSet;
  for (SimplexId i = 0; i < constraintNumber_; ++i) {
    identifiersSet.insert(identifiers[i]);
  }
  // contains vertices with constraints
  std::vector<SimplexId> identifiersVec(identifiersSet.begin(),
                                        identifiersSet.end());

#ifdef TTK_ENABLE_EIGEN
  SpMat lap = compute_laplacian<SpMat, Tri>();

  // constraints vector
  SpVec constraints(vertexNumber_);
  for (auto i : identifiersVec) {
    // TODO are constraints values in outputScalarFieldPointer_?
    constraints.coeffRef(i) = sf[i];
  }

  // penalty matrix
  SpMat penalty(vertexNumber_, vertexNumber_);
  double alpha = 10.0e8;
  for (auto i : identifiersVec) {
    penalty.insert(i, i) = alpha;
  }

  Eigen::SimplicialCholesky<SpMat> solver(lap - penalty);
  SpMat sol = solver.solve(penalty * constraints);

  switch (solver.info()) {
  case Eigen::ComputationInfo::Success:
    cout << "Success!" << endl;
    break;
  case Eigen::ComputationInfo::NumericalIssue:
    cout << "Numerical Issue!" << endl;
    break;
  case Eigen::ComputationInfo::NoConvergence:
    cout << "No Convergence!" << endl;
    break;
  case Eigen::ComputationInfo::InvalidInput:
    cout << "Invalid Input!" << endl;
    break;
  }

  for (SimplexId i = 0; i < vertexNumber_; ++i) {
    // TODO avoid copy here
    sf[i] = sol.coeffRef(i, 0);
  }

  {
    msg << "[HarmonicFieldComputation] Ending computation after"
        << t.getElapsedTime() << endl;
    dMsg(cout, msg.str(), advancedInfoMsg);
  }

#else
  {
    msg << "[HarmonicFieldComputation] Eigen support disabled, computation "
           "skipped "
        << endl;
    dMsg(cout, msg.str(), advancedInfoMsg);
  }
#endif // TTK_ENABLE_EIGEN

  return 0;
}
