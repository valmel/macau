
/*
 * From:
 * http://stackoverflow.com/questions/6142576/sample-from-multivariate-normal-gaussian-distribution-in-c
 */

#include <iostream>
#include <random>                                                                                
#include <Eigen/Dense>
#include <omp.h>
#include <chrono>

#include "mvnormal.h"

using namespace std;
using namespace Eigen;
using namespace std::chrono;

std::mt19937 **bmrngs;

/*
  We need a functor that can pretend it's const,
  but to be a good random number generator 
  it needs mutable state.
*/

#ifdef __INTEL_COMPILER
std::random_device srd;
#pragma omp threadprivate(srd)
#else
// use thread_local for gcc 
thread_local static std::random_device srd;
#endif

#ifndef __clang__
thread_local 
#endif 
static std::mt19937 rng(srd());

#ifndef __clang__
thread_local 
#endif 
static std::normal_distribution<> nd;

double randn(double) {
  return nd(rng);
}

double randn0() {
  return nd(rng);
}

auto
nrandn(int n) -> decltype( VectorXd::NullaryExpr(n, std::cref(randn)) ) 
{
    return VectorXd::NullaryExpr(n, std::cref(randn));
}

void init_bmrng(int seed) {
   int nthreads = -1;
#pragma omp parallel 
   {
#pragma omp single
      {
         nthreads = omp_get_num_threads();
      }
   }
   bmrngs = new std::mt19937*[nthreads];
   for (int i = 0; i < nthreads; i++) {
      bmrngs[i] = new std::mt19937(seed + i * 1999);
   }
}

void init_bmrng() {
   auto ms = (duration_cast< milliseconds >(
             system_clock::now().time_since_epoch()
         )).count();
   init_bmrng(ms);
}

void bmrandn(double* x, long n) {
#pragma omp parallel 
  {
    std::uniform_real_distribution<double> unif(-1.0, 1.0);
    std::mt19937* bmrng = bmrngs[omp_get_thread_num()];
#pragma omp for schedule(static)
    for (long i = 0; i < n; i += 2) {
      double x1, x2, w;
      do {
        x1 = unif(*bmrng);
        x2 = unif(*bmrng);
        w = x1 * x1 + x2 * x2;
      } while ( (w >= 1.0) || (w == 0) );

      w = sqrt( (-2.0 * log( w ) ) / w );
      x[i] = x1 * w;
      if (i + 1 < n) {
        x[i+1] = x2 * w;
      }
    }
  }
}

void bmrandn(MatrixXd & X) {
  long n = X.rows() * (long)X.cols();
  bmrandn(X.data(), n);
}

// to be called within OpenMP parallel loop (also from serial code is fine)
void bmrandn_single(double* x, long n) {
  std::uniform_real_distribution<double> unif(-1.0, 1.0);
  std::mt19937* bmrng = bmrngs[omp_get_thread_num()];
  for (long i = 0; i < n; i += 2) {
    double x1, x2, w;
    do {
      x1 = unif(*bmrng);
      x2 = unif(*bmrng);
      w = x1 * x1 + x2 * x2;
    } while ( (w >= 1.0) || (w == 0) );

    w = sqrt( (-2.0 * log( w ) ) / w );
    x[i] = x1 * w;
    if (i + 1 < n) {
      x[i+1] = x2 * w;
    }
  }
}

void bmrandn_single(Eigen::VectorXd & x) {
  bmrandn_single(x.data(), x.size());
}

/** returns random number according to Gamma distribution
 *  with the given shape (k) and scale (theta). See wiki. */
double rgamma(double shape, double scale) {
  std::gamma_distribution<double> gamma(shape, scale);
  return gamma(*bmrngs[0]);
}

/** Normal(0, Lambda^-1) for nn columns */
MatrixXd MvNormal_prec(const MatrixXd & Lambda, int nn = 1)
{
  int size = Lambda.rows(); // Dimensionality (rows)

  LLT<MatrixXd> chol(Lambda);

  MatrixXd r = MatrixXd::NullaryExpr(size, nn, std::cref(randn));
	chol.matrixU().solveInPlace(r);
  return r;
}

MatrixXd MvNormal_prec(const MatrixXd & Lambda, const VectorXd & mean, int nn = 1)
{
  MatrixXd r = MvNormal_prec(Lambda, nn);
  return r.colwise() + mean;
}

MatrixXd MvNormal_prec_omp(const MatrixXd & Lambda, int nn = 1)
{
  int size = Lambda.rows(); // Dimensionality (rows)

  LLT<MatrixXd> chol(Lambda);

  MatrixXd r(size, nn);
  bmrandn(r);
  // TODO: check if solveInPlace is parallelized:
	chol.matrixU().solveInPlace(r);
  return r;
}

/*
  Draw nn samples from a size-dimensional normal distribution
  with a specified mean and covariance
*/
MatrixXd MvNormal(const MatrixXd covar, const VectorXd mean, int nn = 1) 
{
  int size = mean.rows(); // Dimensionality (rows)
  MatrixXd normTransform(size,size);

  LLT<MatrixXd> cholSolver(covar);
  normTransform = cholSolver.matrixL();

  auto normSamples = MatrixXd::NullaryExpr(size, nn, std::cref(randn));
  MatrixXd samples = (normTransform * normSamples).colwise() + mean;

  return samples;
}

MatrixXd WishartUnit(int m, int df)
{
    MatrixXd c(m,m);
    c.setZero();

    for ( int i = 0; i < m; i++ ) {
        std::gamma_distribution<> gam(0.5*(df - i));
        c(i,i) = sqrt(2.0 * gam(rng));
        VectorXd r = nrandn(m-i-1);
        c.block(i,i+1,1,m-i-1) = r.transpose();
    }

    MatrixXd ret = c.transpose() * c;

#ifdef TEST_MVNORMAL
    cout << "WISHART UNIT {\n" << endl;
    cout << "  m:\n" << m << endl;
    cout << "  df:\n" << df << endl;
    cout << "  ret;\n" << ret << endl;
    cout << "  c:\n" << c << endl;
    cout << "}\n" << ret << endl;
#endif

    return ret;
}

MatrixXd Wishart(const MatrixXd &sigma, const int df)
{
//  Get R, the upper triangular Cholesky factor of SIGMA.
  auto chol = sigma.llt();

//  Get AU, a sample from the unit Wishart distribution.
  MatrixXd au = WishartUnit(sigma.cols(), df);

//  Construct the matrix A = R' * AU * R.
  MatrixXd a = chol.matrixL() * au * chol.matrixU();

#ifdef TEST_MVNORMAL
    cout << "WISHART {\n" << endl;
    cout << "  sigma::\n" << sigma << endl;
    cout << "  r:\n" << r << endl;
    cout << "  au:\n" << au << endl;
    cout << "  df:\n" << df << endl;
    cout << "  a:\n" << a << endl;
    cout << "}\n" << endl;
#endif


  return a;
}


// from julia package Distributions: conjugates/normalwishart.jl
std::pair<VectorXd, MatrixXd> NormalWishart(const VectorXd & mu, double kappa, const MatrixXd & T, double nu)
{
  MatrixXd Lam = Wishart(T, nu);
  MatrixXd mu_o = MvNormal_prec(Lam * kappa, mu);

#ifdef TEST_MVNORMAL
    cout << "NORMAL WISHART {\n" << endl;
    cout << "  mu:\n" << mu << endl;
    cout << "  kappa:\n" << kappa << endl;
    cout << "  T:\n" << T << endl;
    cout << "  nu:\n" << nu << endl;
    cout << "  mu_o\n" << mu_o << endl;
    cout << "  Lam\n" << Lam << endl;
    cout << "}\n" << endl;
#endif

  return std::make_pair(mu_o , Lam);
}

std::pair<VectorXd, MatrixXd> OldCondNormalWishart(const MatrixXd &U, const VectorXd &mu, const double kappa, const MatrixXd &T, const int nu)
{
  int N = U.cols();

  auto Um = U.rowwise().mean();

  // http://stackoverflow.com/questions/15138634/eigen-is-there-an-inbuilt-way-to-calculate-sample-covariance
  MatrixXd C = U.colwise() - Um;
  MatrixXd S = (C * C.adjoint()) / double(N - 1);
  VectorXd mu_c = (kappa*mu + N*Um) / (kappa + N);
  double kappa_c = kappa + N;
  MatrixXd T_c = ( T + N * S.transpose() + (kappa * N)/(kappa + N) * (mu - Um) * ((mu - Um).transpose())).inverse();
  int nu_c = nu + N;

#ifdef TEST_MVNORMAL
  cout << "mu_c:\n" << mu_c << endl;
  cout << "kappa_c:\n" << kappa_c << endl;
  cout << "T_c:\n" << T_c << endl;
  cout << "nu_c:\n" << nu_c << endl;
#endif

  return NormalWishart(mu_c, kappa_c, T_c, nu_c);
}

// from bpmf.jl -- verified
std::pair<VectorXd, MatrixXd> CondNormalWishart(const MatrixXd &U, const VectorXd &mu, const double kappa, const MatrixXd &T, const int nu)
{
  /// TODO: parallelize (for computing C and C * C')
  int N = U.cols();

  VectorXd Um = U.rowwise().mean();

  // http://stackoverflow.com/questions/15138634/eigen-is-there-an-inbuilt-way-to-calculate-sample-covariance
  auto C = U.colwise() - Um;
  MatrixXd S = (C * C.adjoint()) / double(N - 1);
  VectorXd mu_c = (kappa*mu + N*Um) / (kappa + N);
  double kappa_c = kappa + N;
  auto mu_m = (mu - Um);
  double kappa_m = (kappa * N)/(kappa + N);
  auto X = ( T + N * S + kappa_m * (mu_m * mu_m.transpose()));
  MatrixXd T_c = X.inverse();
  int nu_c = nu + N;

#ifdef TEST_MVNORMAL
  cout << "mu_c:\n" << mu_c << endl;
  cout << "kappa_c:\n" << kappa_c << endl;
  cout << "T_c:\n" << T_c << endl;
  cout << "nu_c:\n" << nu_c << endl;
#endif

  return NormalWishart(mu_c, kappa_c, T_c, nu_c);
}

#if defined(TEST_MVNORMAL) || defined (BENCH_MVNORMAL)

int main()
{

    MatrixXd U(32,32 * 1024);
    U.setOnes();

    VectorXd mu(32);
    mu.setZero();

    double kappa = 2;

    MatrixXd T(32,32);
    T.setIdentity(32,32);
    T.array() /= 4;

    int nu = 3;

    VectorXd mu_out;
    MatrixXd T_out;

#ifdef BENCH_MVNORMAL
    for(int i=0; i<300; ++i) {
        tie(mu_out, T_out) = CondNormalWishart(U, mu, kappa, T, nu);
        cout << i << "\r" << flush;
    }
    cout << endl << flush;

    for(int i=0; i<7; ++i) {
        cout << i << ": " << (int)(100.0 * acc[i] / acc[7])  << endl;
    }
    cout << "total: " << acc[7] << endl;

    for(int i=0; i<300; ++i) {
        tie(mu_out, T_out) = OldCondNormalWishart(U, mu, kappa, T, nu);
        cout << i << "\r" << flush;
    }
    cout << endl << flush;

    cout << "total: " << acc[8] << endl;


#else
#if 1
    cout << "COND NORMAL WISHART\n" << endl;

    tie(mu_out, T_out) = CondNormalWishart(U, mu, kappa, T, nu);

    cout << "mu_out:\n" << mu_out << endl;
    cout << "T_out:\n" << T_out << endl;

    cout << "\n-----\n\n";
#endif

#if 0
    cout << "NORMAL WISHART\n" << endl;

    tie(mu_out, T_out) = NormalWishart(mu, kappa, T, nu);
    cout << "mu_out:\n" << mu_out << endl;
    cout << "T_out:\n" << T_out << endl;

#endif

#if 0
    cout << "MVNORMAL\n" << endl;
    MatrixXd out = MvNormal(T, mu, 10);
    cout << "mu:\n" << mu << endl;
    cout << "T:\n" << T << endl;
    cout << "out:\n" << out << endl;
#endif
#endif
}

#endif
