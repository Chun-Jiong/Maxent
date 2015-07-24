/*****************************************************************************
 *
 * ALPS Project Applications
 *
 * Copyright (C) 2010 by Sebastian Fuchs <fuchs@comp-phys.org>
 *                       Thomas Pruschke <pruschke@comp-phys.org>
 *                       Matthias Troyer <troyer@comp-phys.org>
 *
 * This software is part of the ALPS Applications, published under the ALPS
 * Application License; you can use, redistribute it and/or modify it under
 * the terms of the license, either version 1 or (at your option) any later
 * version.
 * 
 * You should have received a copy of the ALPS Application License along with
 * the ALPS Applications; see the file LICENSE.txt. If not, the license is also
 * available from http://alps.comp-phys.org/.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT 
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE 
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE, 
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 * DEALINGS IN THE SOFTWARE.
 *
 *****************************************************************************/
#include "maxent_kernel.hpp"
#include <cmath>
#include <boost/algorithm/string.hpp>    
#include <boost/lexical_cast.hpp>
#include <boost/math/special_functions/legendre.hpp>    //for Legendre kernel
#include <boost/math/special_functions/factorials.hpp>  //for Legendre kernel
#include <gsl/gsl_integration.h>                        //for Legendre Kernel

namespace bmth = boost::math;

kernel::kernel(const alps::params &p, const vector_type& freq, const int lmax):
ndat_(p["NDAT"]),
nfreq_(p["NFREQ"]),
T_(1./static_cast<double>(p["BETA"])),
K_(ndat_,nfreq_)
{
  using namespace boost::numeric;
  //K_.clear();
  K_=matrix_type::Zero(ndat_,nfreq_);
  std::string dataspace_name = p["DATASPACE"];
  std::string kernel_name = p["KERNEL"];
  boost::to_lower(dataspace_name);
  boost::to_lower(kernel_name);
  bool ph_symmetry=p["PARTICLE_HOLE_SYMMETRY"];
  bool legdr_transform=p["LEGENDRE"];
  std::cout<<"using kernel "<<kernel_name<<" in domain "<<dataspace_name;
  if(ph_symmetry) std::cout<<" with ph symmetry"; else std::cout<<" without ph symmetry"; std::cout<<std::endl;

  set_kernel_type(dataspace_name,kernel_name, ph_symmetry,legdr_transform);
    
  if(ktype_==legendre_fermionic_kernel || ktype_==legendre_bosonic_kernel){
      setup_legendre_kernel(p,freq,ndat_);
  }else if(ktype_==time_fermionic_kernel){
      for (int i=0; i<ndat_; ++i) {
        double tau;
        if (p.defined("TAU_"+boost::lexical_cast<std::string>(i))) //TODO: why is tau here and not centralized
          tau = p["TAU_"+boost::lexical_cast<std::string>(i)];     //like the freq below
        else
          tau = i / ((ndat_-1)* T_);
        for (int j=0; j<nfreq_; ++j) {
          double omega = freq[j]; //Default().omega_of_t(double(j)/(nfreq_-1));
          K_(i,j) =  -1. / (std::exp(omega*tau) + std::exp(-omega*(1./T_-tau)));
        }
      }
    }
    else if (ktype_==time_bosonic_kernel) {
      for (int i=0; i<ndat_; ++i) {
        double tau;
        if (p.defined("TAU_"+boost::lexical_cast<std::string>(i)))
          tau = p["TAU_"+boost::lexical_cast<std::string>(i)];
        else
          tau = i / ((ndat_-1) * T_);
        K_(i,0) = T_;
        for (int j=1; j<nfreq_; ++j) {
          double omega = freq[j];
          K_(i,j) = 0.5*omega * (std::exp(-omega*tau) + std::exp(-omega*(1./T_-tau))) / (1 - std::exp(-omega/T_));
        }
      }
    }
    else if(ktype_== time_fermionic_legendre_kernel || ktype_==time_bosonic_legendre_kernel)
        setup_legendre_kernel(p,freq,lmax);
    //for zero temperature, only positive frequency matters
    else if (ktype_ == time_boris_kernel) {
      for (int i=0; i<ndat_; ++i) {
        double tau = p["TAU_"+boost::lexical_cast<std::string>(i)];
        for (int j=0; j<nfreq_; ++j) {
          double omega = freq[j];
          K_(i,j) = -std::exp(-omega*tau);
        }
      }
    }
    else if(ktype_==time_fermionic_kernel){
        for (int i=0; i<ndat_; ++i) {
            double tau;
            if (p.defined("TAU_"+boost::lexical_cast<std::string>(i)))
                tau = p["TAU_"+boost::lexical_cast<std::string>(i)];
            else
                tau = i / ((ndat_-1)* T_);
            for (int j=0; j<nfreq_; ++j) {
                double omega = freq[j];
                K_(i,j) =  -1.;
            }
        }

    }
    else if(ktype_==frequency_fermionic_ph_kernel) {
    for (int i=0; i<ndat_; ++i) {
      double omegan = (2*i+1)*M_PI*T_;
      for (int j=0; j<nfreq_; ++j) {
        double omega = freq[j];
        K_(i,j) =  -omegan / (omegan*omegan + omega*omega);
      }
    }
  }
  else if (ktype_==frequency_bosonic_ph_kernel) {
    for (int i=0; i<ndat_; ++i) {
      double Omegan = (2*i)*M_PI*T_;
      for (int j=0; j<nfreq_; ++j) {
        double Omega = freq[j];
        if(Omega ==0) throw std::runtime_error("Bosonic kernel is singular at frequency zero. Please use grid w/o evaluation at zero.");
        K_(i,j) =  -Omega*Omega / (Omegan*Omegan + Omega*Omega);
      }
    }
  }else if (ktype_==frequency_anomalous_ph_kernel) {
    for(int i=0;i<ndat_;++i){
      double omegan = (2*i+1)*M_PI*T_;
      for (int j=0; j<nfreq_; ++j) {
        double omega = freq[j];
        K_(i,j) =  omega*omega / (omegan*omegan + omega*omega);
      }
    }
  }
  else{
    complex_matrix_type Kc(ndat_/2, nfreq_);
    if (ktype_==frequency_fermionic_kernel) {
      ///ndat/2 is defined as such below because ndat=number of points inputed
      ///if ph symmetry, then ndat=number of imag points
      ///otherwise ndat=total number of real+imag points = ndat/2 data points
      for (int i=0; i<ndat_/2; ++i) {
        std::complex<double> iomegan(0, (2*i+1)*M_PI*T_);
        for (int j=0; j<nfreq_; ++j) {
          double omega = freq[j];
          Kc(i,j) =  1. / (iomegan - omega);
        }
      }
    }
    else if (ktype_==frequency_bosonic_kernel){
      for (int i=0; i<ndat_/2; ++i) {
        std::complex<double> iomegan(0, 2*i*M_PI*T_);
        for (int j=1; j<nfreq_; ++j) {
          double omega = freq[j];
          Kc(i,j) =  omega / (iomegan - omega);
        }
      }
    }
    else if (ktype_==frequency_anomalous_kernel){
      for (int i=0; i<ndat_/2; ++i) {
        std::complex<double> iomegan(0, (2*i+1)*M_PI*T_);
        for (int j=1; j<nfreq_; ++j) {
          double omega = freq[j];
          Kc(i,j) =  -omega / (iomegan - omega);
        }
      }
    }else
      throw std::logic_error("unknown kernel type");

    for (int i=0; i<ndat_; i+=2) {
      //TODO: understand the j=1 here
      for (int j=0; j<nfreq_; ++j) {
        K_(i,j) = Kc(i/2,j).real();
        K_(i+1,j) = Kc(i/2,j).imag();
      }
    }
  }
}

void kernel::set_kernel_type(const std::string &dataspace_name, const std::string &kernel_name,
                             bool ph_symmetry, bool legdr_transform){
  if(dataspace_name=="time"){
    dtype_=time_dataspace;
  }else if(dataspace_name=="frequency"){
    dtype_=frequency_dataspace;
  }else if(dataspace_name=="legendre"){
    dtype_=legendre_dataspace;
  }
  else
    throw std::invalid_argument("unknown dataspace name. it should be time or frequency");

  if(dtype_==time_dataspace){
    if(kernel_name=="fermionic")
            if(legdr_transform)
                ktype_=time_fermionic_legendre_kernel;
            else
                ktype_=time_fermionic_kernel;
    else if(kernel_name=="bosonic")
            if(legdr_transform)
                ktype_=time_bosonic_legendre_kernel;
            else
                ktype_=time_bosonic_kernel;
    else if(kernel_name=="boris")
      ktype_=time_boris_kernel;
    else throw std::invalid_argument("unknown kernel name. In the time domain it should be fermionic, bosonic, or boris.");
  }else if(dtype_ == legendre_dataspace){
      if(kernel_name=="fermionic")
          ktype_=legendre_fermionic_kernel;
      else if(kernel_name=="bosonic"){
          ktype_=legendre_bosonic_kernel;
      }
      else throw std::invalid_argument("unknown kernel name. In the legendre domain it should be fermionic or bosonic");
  }
  else{
    if(ph_symmetry){
      if(kernel_name== "fermionic")
        ktype_=frequency_fermionic_ph_kernel;
      else if (kernel_name=="bosonic")
        ktype_=frequency_bosonic_ph_kernel;
      else if (kernel_name=="anomalous")
        ktype_=frequency_anomalous_ph_kernel;
      else throw std::invalid_argument("unknown kernel name. In the particle hole symmetric frequency domain it should be fermionic, bosonic, or anomalous.");
    }else{
      if(kernel_name== "fermionic")
        ktype_=frequency_fermionic_kernel;
      else if (kernel_name=="bosonic")
        ktype_=frequency_bosonic_kernel;
      else if (kernel_name=="anomalous")
        ktype_=frequency_anomalous_kernel;
      else throw std::invalid_argument("unknown kernel name. In the non-particle hole symmetric frequency domain it should be fermionic, bosonic, or anomalous.");
    }
  }

}
struct integrand_params {int l; double omega; double sign; double T_;};

///Integrand of the Legendre Kernel for GSL integration
double  legendre_kernel_integrand(double x, void * params){
    double tau = x;
    //parms = [l,omega,sign,T_]
    integrand_params *p = (integrand_params *)params;
    int l = p->l;
    double omega = p->omega;
    double sign = p->sign;
    double T_ = p->T_;
    //std::cout<< l<< std::endl;
    return bmth::legendre_p(l, 2*tau*T_-1)*std::exp(-tau*omega)/(1+sign*std::exp(-omega/T_));
}
void kernel::setup_legendre_kernel(const alps::params &p, const vector_type& freq,const int lmax){
    if(lmax>boost::math::max_factorial<double>::value)
        throw std::runtime_error("lmax is greater than boost factorial precision");
    
    //sign of kernel +/- => fermionic/bosonic
    const double sign =std::pow(-1.0,(int)ktype_==time_bosonic_legendre_kernel);

    const double PI = std::acos(-1);
    const std::complex<double> CONE(0,1);
    //recall that ndat()=lmax, lmax=size of K
    //here ndat_ = # tau points
    K_.resize(lmax,nfreq_);
    vector_type tau_points(ndat_); //TODO: make a seperate implimentation that imports this
    if(p.defined("TAU_1")) //hack to see if we have imported tau points
        for(int j=0;j<ndat_;j++)
            tau_points[j]=p["TAU_"+boost::lexical_cast<std::string>(j)];
    else
        for(int j=0;j<ndat_;j++)
            tau_points[j] = j / ((ndat_)* T_); //TODO: determine if this (from backcont) or ndat()-1 is more common
    int N = 20000/2;
    gsl_integration_workspace *w = gsl_integration_workspace_alloc (1000);
    
    for(int l=0;l<lmax;l++){
        for(int j=0;j<nfreq_;j++){
            double I=1;
            double I1=0;
            double omega =freq[j];
            double h = (1/T_-0)/(2*N);
            //int Pl(x(tau))*exp(-tau*omega)/(1\pm exp(-beta*omega))
            
           /* //Riemann sum method
            for(int t=0;t<ndat_-1;t++){
                double tau = tau_points[t];
                double dtau = tau_points[t+1]-tau;
                I1+= bmth::legendre_p(l, 2*tau*T_-1)*std::exp(-tau*omega)/(1+sign*std::exp(-omega/T_))*dtau;
            }
            double tau = tau_points[ndat_-1];
            double dtau = tau-tau_points[ndat_-2];
            I1+= bmth::legendre_p(l, 2*tau*T_-1)*std::exp(-tau*omega)/(1+sign*std::exp(-omega/T_))*dtau;
            */
            //Simpsons with
            //Simpson's method of integrations
            //eval endpoints
            /*I1 += bmth::legendre_p(l, 1.0)*std::exp(-0*omega)/(1+sign*std::exp(-omega/T_));
            I1 += bmth::legendre_p(l, -1.0)*std::exp(-omega/T_)/(1+sign*std::exp(-omega/T_));
            for(int i=1;i<N;i++){
                double tau = 0 + 2*i*h;
                I1+=2*bmth::legendre_p(l, 2*tau*T_-1)*std::exp(-tau*omega)/(1+sign*std::exp(-omega/T_));
            }
            for(int i=1;i<N+1;i++){
                double tau= 0+ (2*i-1)*h;
                I1+=4*bmth::legendre_p(l, 2*tau*T_-1)*std::exp(-tau*omega)/(1+sign*std::exp(-omega/T_));
            }
            I1*=h/3;//*/
            ///*
            double a = 0;
            double b = 1/T_;
            double epsabs=1.49e-08; //python default resolution
            double epsrel=1.49e-08;
            double result,err;
            size_t nval,limit=500;
            
            gsl_function F;
            F.function = &legendre_kernel_integrand;
            //double p[4] = {l,omega,sign,T_};
            integrand_params p = {l,omega,sign,T_};
            F.params = &p;
            //gsl_integration_qng(&F,a,b,epsabs,epsrel,&result,&err,&nval);
            gsl_integration_qag(&F, a,b,epsabs,epsrel, limit, GSL_INTEG_GAUSS61, w, &result, &err);
            
            I1=result; //*/
            
            //commented out integrated Kernel, until convergence is explained
            //integrated form of Kernel:
            /*I *= 1/(2*T_)*1/(1+sign*exp(-omega/T_));
            double Ip=0;
            for(int v=0;v<=l;v++){
                Ip+=2*std::pow(omega/T_,-v-1)*bmth::factorial<double>(l+v)/
                (bmth::factorial<double>(v)*bmth::factorial<double>(l-v))*(std::pow(-1.0,l+v)-exp(-omega/T_));
            }
            I*=Ip;
            I1=I;
            /*double err2 = I1-I;
            if(std::abs(err2)>0.1)
                std::cout<<err2<<" " << l<<" " << j<<" " <<omega << std::endl;
            if(std::abs(omega)>1)
                K_(l,j) = -sqrt(2*l+1)*I; //TODO: pi issues
            else*/
                K_(l,j) = -sqrt(2*l+1)*I1;
            
            
        }
    }
    gsl_integration_workspace_free (w);
}

