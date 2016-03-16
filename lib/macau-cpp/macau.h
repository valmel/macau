#ifndef MACAU_H
#define MACAU_H

#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <unsupported/Eigen/SparseExtra>
#include "latentprior.h"

// try adding num_latent as template parameter to Macau
class Macau {
  public:
  int num_latent;

  double alpha = 2.0; 
  int nsamples = 100;
  int burnin   = 50;

  double mean_rating = .0; 
  Eigen::SparseMatrix<double> Y, Yt, Ytest;

  double rmse_test  = .0;
  double rmse_train = .0;

  /** BPMF model */
  std::vector<ILatentPrior*> priors;
  std::vector<Eigen::MatrixXd*> samples;
//  BPMFPrior prior_u;
//  BPMFPrior prior_m;
//  Eigen::MatrixXd sample_u;
//  Eigen::MatrixXd sample_m;

  public:
    Macau(int D) : num_latent{D} {}
    Macau() : Macau(10) {}
    void addPrior(ILatentPrior* prior);
    void setPrecision(double p);
    void setSamples(int burnin, int nsamples);
    void setRelationData(int* rows, int* cols, double* values, int N, int nrows, int ncols);
    void setRelationDataTest(int* rows, int* cols, double* values, int N, int nrows, int ncols);
    double getRmseTest();
    void init();
    void run();
    ~Macau();
};

void sparseFromIJV(Eigen::SparseMatrix<double> & X, int* rows, int* cols, double* values, int N);

std::pair<double,double> eval_rmse(Eigen::SparseMatrix<double> & P, int n, Eigen::VectorXd & predictions, const Eigen::MatrixXd &sample_m, const Eigen::MatrixXd &sample_u, double mean_rating);

#endif /* MACAU_H */
