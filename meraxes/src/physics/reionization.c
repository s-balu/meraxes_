#include "meraxes.h"
#include <math.h>

void calculate_Mvir_crit(run_globals_t *run_globals, double redshift, float *z_at_ionization, float *J_at_ionization, float *Mvir_crit)
{
#ifdef USE_TOCF
  // Calculate the critical Mvir value in each grid cell (ala Sobacchi & Mesinger 2013b)
  
  int HII_dim = tocf_params.HII_dim;
  int n_cell = pow(HII_dim, 3);
  int cell_Mvir_crit;
  float Mvir_atomic = tocf_params.ion_tvir_min;

  float m_0_sm = tocf_params.m_0_sm;
  float a_sm = tocf_params.a_sm;
  float b_sm = tocf_params.b_sm;
  float c_sm = tocf_params.c_sm;
  float d_sm = tocf_params.d_sm;

  // init
  for(int ii=0; ii<n_cell; ii++)
    Mvir_crit[ii] = 0;

  for(int ii=0; ii<HII_dim; ii++)
  {
    for(int jj=0; jj<HII_dim; jj++)
    {
      for(int kk=0; kk<HII_dim; kk++)
      {
        cell_Mvir_crit = Mvir_atomic;
        if(z_at_ionization[HII_R_INDEX(ii,jj,kk)] > redshift)
          cell_Mvir_crit = m_0_sm*pow((1.0+redshift)/10.0, a_sm) * pow(J_at_ionization[HII_R_INDEX(ii,jj,kk)], b_sm)*
            pow((1.0-pow((1.0+redshift)/(1.0+z_at_ionization[HII_R_INDEX(ii,jj,kk)]), c_sm)), d_sm);
        Mvir_crit[HII_R_INDEX(ii,jj,kk)] = (Mvir_atomic > cell_Mvir_crit ? Mvir_atomic : cell_Mvir_crit);
      }
    }
  }
#endif
}

bool check_reionization_cooling(float cell_ionization, float Vvir)
{

  float Tvir;
  bool   flag;

  if(cell_ionization>0.995)
  {
    Tvir = 35.9 * (Vvir*Vvir); 
    flag = (Tvir < 1e5) ? false : true;
  } else
    flag = true;

  return flag;

}