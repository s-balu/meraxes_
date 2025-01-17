#include <complex.h>
#include <fftw3.h>
#include <gsl/gsl_rng.h>
#include <hdf5.h>
#include <mlog.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef CALC_MAGS
#include <sector.h>
#endif

#ifndef _INIT_MERAXES
#define _INIT_MERAXES

/*
 * Definitions
 */

#define N_HISTORY_SNAPS @N_HISTORY_SNAPS@
#define MERAXES_GITREF_STR "@GITREF@"
#define MERAXES_GITDIFF_STR "@GITDIFF@"

#ifdef CALC_MAGS
#define MAGS_N_SNAPS @MAGS_N_SNAPS@
#define MAGS_N_BANDS @MAGS_N_BANDS@
#define MAGS_N MAGS_N_SNAPS*MAGS_N_BANDS
#endif

// ======================================================
// Don't change these unless you know what you are doing!
#define STRLEN 256 //!< Default string length
// ======================================================

// Define things used for aborting exceptions
#ifdef __cplusplus
extern "C" {
#endif
    void myexit(int signum);
#ifdef __cplusplus
}
#endif
#define ABORT(sigterm)                                                                 \
    do {                                                                                   \
        fprintf(stderr, "\nIn file: %s\tfunc: %s\tline: %i\n", __FILE__, __FUNCTION__, __LINE__);  \
        myexit(sigterm);                                                                     \
    } while (0)

// Units (cgs):
#define GRAVITY 6.672e-8
#define SOLAR_MASS 1.989e33
#define SOLAR_LUM 3.826e33
#define RAD_CONST 7.565e-15
#define AVOGADRO 6.0222e23
#define BOLTZMANN 1.3806e-16
#define GAS_CONST 8.31425e7
#define C 2.9979e10
#define PLANCK 6.6262e-27 //! [erg/s]
#define PROTONMASS 1.6726e-24
#define HUBBLE 3.2407789e-18 //! [h/sec]
#define SEC_PER_MEGAYEAR 3.155e13
#define SEC_PER_YEAR 3.155e7
#define MPC 3.086e24
#define TCMB 2.728

// Constants relevant for spin temperature (taken from 21cmFAST)
#define CLUMPING_FACTOR (double) (2) // sub grid scale.  note that if you want to run-down from a very high redshift (>50), you should set this to one..
#define NU_over_EV (double) (1.60217646e-12 / PLANCK)
#define NUIONIZATION (double) (13.60*NU_over_EV)  /* ionization frequency of H */
#define HeI_NUIONIZATION (double) (24.59*NU_over_EV) /* ionization frequency of HeI */
#define HeII_NUIONIZATION (double) (NUIONIZATION*4) /* ionization frequency of HeII */
#define T21 (double) (0.0628) /* temperature corresponding to the 21cm photon */
#define A10_HYPERFINE (double) (2.85e-15) /* spontaneous emission coefficient in s^-1 */
#define Ly_alpha_HZ  (double ) (2.46606727e15)  /* frequency of Lyalpha */
#define R_XLy_MAX (float) (500)
#define SIGMA_HI (double) (6.3e-18)  /* HI ionization  cross section at 13.6 eV in cm^-2 */
#define TINY (double) (1e-30)

// Note these have the hubble (little h) factor included (taken from 21cmFAST)
#define RHOcrit (double) ( (3.0*HUBBLE*HUBBLE*run_globals.params.Hubble_h*run_globals.params.Hubble_h / (8.0*M_PI*GRAVITY)) * (MPC*MPC*MPC)/SOLAR_MASS) /* Msun Mpc^-3 */ /* at z=0 */
#define RHOcrit_cgs (double) (3.0*HUBBLE*HUBBLE*run_globals.params.Hubble_h*run_globals.params.Hubble_h / (8.0*M_PI*GRAVITY)) /* g pcm^-3 */ /* at z=0 */
#define OMb (run_globals.params.BaryonFrac*run_globals.params.OmegaM)
#define No  (double) (RHOcrit_cgs*OMb*(1-run_globals.params.physics.Y_He)/PROTONMASS)  /*  current hydrogen number density estimate  (#/cm^3)  ~1.92e-7*/
#define He_No (double) (RHOcrit_cgs*OMb*run_globals.params.physics.Y_He/(4.0*PROTONMASS)) /*  current helium number density estimate */
#define f_H (double) (No/(No+He_No))  /* hydrogen number fraction */
#define f_He (double) (He_No/(No+He_No))  /* helium number fraction */
#define FRACT_FLOAT_ERR (double) (1e-7) /* fractional floating point error */
#define N_b0 (double) (No+He_No) /* present-day baryon num density, H + He */

#define N_RSD_STEPS (int) (50)

// Parameters taken from 21cmFAST
#define MAX_TK (float) 5e4
#define L_FACTOR 0.620350491 // Factor relating cube length to filter radius = (4PI/3)^(-1/3)
#define MAX_DVDR (float) (0.2)

#define alphaB_10k (double) (2.59e-13) /* taken from osterbrock for T=10000 */

// Constants
#define REL_TOL (float)1e-5
#define ABS_TOL (float)1e-8

/*
 * Enums
 */
typedef enum index_type {
    INDEX_PADDED = 5674,
    INDEX_REAL,
    INDEX_COMPLEX_HERM,
} index_type;

typedef enum SFtype { 
    INSITU,
    MERGER
} SFtype;

// N.B. Don't change these values!
enum grid_prop {
    DENSITY = 0,
    X_VELOCITY = 1,
    Y_VELOCITY = 2,
    Z_VELOCITY = 3
};

/*
 * Structures
 */

//! Utility timer struct for GPU runs
typedef struct timer_info {
    struct timeval start;
    struct timeval stop;
} timer_info;

#ifdef _MAIN
float timer_gpu = 0.f;
#else
extern float timer_gpu;
#endif

//! Physics parameter values
typedef struct physics_params_t {
    double SfEfficiency;
    double SfEfficiencyScaling;
    double SfCriticalSDNorm;
    double SfRecycleFraction;
    int SnModel;
    double SnReheatRedshiftDep;
    double SnReheatEff;
    double SnReheatLimit;
    double SnReheatScaling;
    double SnReheatScaling2;
    double SnReheatNorm;
    double SnEjectionRedshiftDep;
    double SnEjectionEff;
    double SnEjectionScaling;
    double SnEjectionScaling2;
    double SnEjectionNorm;
    double MaxCoolingMassFactor;
    int ReincorporationModel;
    double ReincorporationEff;
    double Yield;
    double RadioModeEff;
    double QuasarModeEff;
    double BlackHoleGrowthRate;
    double EddingtonRatio;
    double quasar_mode_scaling;
    double quasar_open_angle;
    double quasar_fobs;

    double ThreshMajorMerger;
    double MinMergerStellarMass;
    double MinMergerRatioForBurst;
    double MergerBurstScaling;
    double MergerBurstFactor;
    double MergerTimeFactor;

    // TODO: These parameters should be used to set the TOCF HII_EFF_FACTOR value
    double ReionEfficiency;
    double ReionNionPhotPerBary;
    double BlackHoleSeed;
    double BlackHoleMassLimitReion;
    double ReionTcool;
    double Y_He;

    // Parameters to describe the X-ray properties of the sources
    // Keeping the QSO and Galaxy components separate for now (might be combined in the end)
    double LXrayGal;
    double NuXrayGalThreshold;
    double SpecIndexXrayGal;
    double LXrayQSO;
    double NuXrayQSOThreshold;
    double SpecIndexXrayQSO;
    double NuXraySoftCut;
    double NuXrayMax;

    double ReionMaxHeatingRedshift;

    double ReionGammaHaloBias;
    double ReionAlphaUV;
    double ReionAlphaUVBH;
    double ReionRBubbleMin;
    double ReionRBubbleMax;
    double ReionRBubbleMaxRecomb;

    double EscapeFracNorm;
    double EscapeFracRedshiftScaling;
    double EscapeFracPropScaling;
    double EscapeFracBHNorm;
    double EscapeFracBHScaling;

    // global reionization prescription
    double ReionSobacchi_Zre;
    double ReionSobacchi_DeltaZre;
    double ReionSobacchi_DeltaZsc;
    double ReionSobacchi_T0;

    // global reionization prescription
    double ReionGnedin_z0;
    double ReionGnedin_zr;

    // filtering mass fit
    double ReionSMParam_m0;
    double ReionSMParam_a;
    double ReionSMParam_b;
    double ReionSMParam_c;
    double ReionSMParam_d;

    // options
    int EscapeFracDependency;
    int SfDiskVelOpt;
    int SfPrescription;

    // Flags
    int Flag_ReionizationModifier;
    int Flag_BHFeedback;
    int Flag_IRA;
    int Flag_FixDiskRadiusOnInfall;
    int Flag_FixVmaxOnInfall;
    int Flag_ReheatToFOFGroupTemp;
} physics_params_t;

enum tree_ids { VELOCIRAPTOR_TREES,
    GBPTREES_TREES };

//! Run params
//! Everything in this structure is supplied by the user...
typedef struct run_params_t {
    char DefaultsFile[STRLEN];
    char OutputDir[STRLEN];
    char FileNameGalaxies[STRLEN];
    char SimName[STRLEN];
    char SimulationDir[STRLEN];
    char CatalogFilePrefix[STRLEN];
    char FileWithOutputSnaps[STRLEN];
    char PhotometricTablesDir[STRLEN];
    char TargetSnaps[STRLEN];
    char BetaBands[STRLEN];
    char RestBands[STRLEN];
    double BirthCloudLifetime;
    char CoolingFuncsDir[STRLEN];
    char StellarFeedbackDir[STRLEN];
    char TablesForXHeatingDir[STRLEN];
    char IMF[STRLEN];
    char MagSystem[STRLEN];
    char MagBands[STRLEN];
    char ForestIDFile[STRLEN];
    char MvirCritFile[STRLEN];
    char MassRatioModifier[STRLEN];
    char BaryonFracModifier[STRLEN];

    physics_params_t physics;

    double BoxSize;
    double VolumeFactor;
    double Hubble_h;
    double BaryonFrac;
    double OmegaM;
    double OmegaK;
    double OmegaR;
    double OmegaLambda;
    double Sigma8;
    double wLambda;
    double SpectralIndex;
    double PartMass;
    long long NPart;

    double* MvirCrit;

    double ReionDeltaRFactor;
    double ReionPowerSpecDeltaK;
    int ReionGridDim;
    int ReionFilterType;
    int TsHeatingFilterType;
    int ReionRtoMFilterType;
    int ReionUVBFlag;

    enum tree_ids TreesID;
    int FirstFile;
    int LastFile;
    int NSteps;
    int SnaplistLength;
    int RandomSeed;
    int FlagSubhaloVirialProps;
    int FlagInteractive;
    int FlagMCMC;
    int Flag_PatchyReion;
    int Flag_IncludeSpinTemp;
    int Flag_IncludeRecombinations;
    int Flag_Compute21cmBrightTemp;
    int Flag_ComputePS;
    int Flag_IncludePecVelsFor21cm;
    int Flag_ConstructLightcone;

    int TsVelocityComponent;
    int TsNumFilterSteps;

    double ReionSfrTimescale;

    double EndRedshiftLightcone;
    int EndSnapshotLightcone;
    long long LightconeLength;
    long long CurrentLCPos;
    int PS_Length;
    int Flag_SeparateQSOXrays;
    int Flag_OutputGrids;
    int Flag_OutputGridsPostReion;
    int FlagIgnoreProgIndex;
} run_params_t;

typedef struct run_units_t {
    double UnitTime_in_s;
    double UnitLength_in_cm;
    double UnitVelocity_in_cm_per_s;
    double UnitTime_in_Megayears;
    double UnitMass_in_g;
    double UnitDensity_in_cgs;
    double UnitPressure_in_cgs;
    double UnitCoolingRate_in_cgs;
    double UnitEnergy_in_cgs;
    // TOTAL : 72  (must be multiple of 8)
} run_units_t;

typedef struct hdf5_output_t {
    char** params_tag;
    void** params_addr;
    int* params_type;
    size_t* dst_offsets;
    size_t* dst_field_sizes;
    const char** field_names;
    const char** field_units;
    const char** field_h_conv;
    hid_t* field_types;
    size_t dst_size;
    hid_t array3f_tid; // sizeof(hid_t) = 4
    hid_t array_nmag_f_tid;
    hid_t array_nhist_f_tid;
    int n_props;
    int params_count;

    // TOTAL : 52 + 4 padding (must be multiple of 8)
} hdf5_output_t;

typedef struct gal_to_slab_t {
    int index;
    struct galaxy_t* galaxy;
    int slab_ind;
} gal_to_slab_t;

typedef struct reion_grids_t {
    ptrdiff_t* slab_nix;
    ptrdiff_t* slab_ix_start;
    ptrdiff_t* slab_n_complex;

    float* buffer;
    float* stars;
    float* stars_temp;
    fftwf_complex* stars_unfiltered;
    fftwf_complex* stars_filtered;
    float* deltax;
    float* deltax_temp;
    fftwf_complex* deltax_unfiltered;
    fftwf_complex* deltax_filtered;
    float* sfr;
    float* sfr_temp;
    fftwf_complex* sfr_unfiltered;
    fftwf_complex* sfr_filtered;
    float* xH;
    float* z_at_ionization;
    float* J_21_at_ionization;
    float* J_21;
    float* Mvir_crit;
    float* r_bubble;

    // Grids necessary for the IGM spin temperature
    fftwf_complex* x_e_unfiltered;
    fftwf_complex* x_e_filtered;
    float* x_e_box;
    float* x_e_box_prev;
    float* Tk_box;
    float* Tk_box_prev;
    float* TS_box;

    double* SMOOTHED_SFR_GAL;
    double* SMOOTHED_SFR_QSO;

    // Grids necessary for inhomogeneous recombinations
    fftwf_complex* N_rec_unfiltered;
    fftwf_complex* N_rec_filtered;
    float* z_re;
    float* N_rec;
    float* N_rec_prev;
    float* Gamma12;

    // Grids necessary for the 21cm brightness temperature
    float* delta_T;
    float* delta_T_prev;
    float* vel;
    float* vel_temp;
    fftwf_complex* vel_gradient;

    // Grid for the lightcone (cuboid) box
    float* LightconeBox;
    float* Lightcone_redshifts;

    // Data for the power spectrum
    float *PS_k;
    float *PS_data;
    float *PS_error;

    gal_to_slab_t* galaxy_to_slab_map;

    double volume_weighted_global_xH;
    double mass_weighted_global_xH;


    double volume_ave_J_alpha;
    double volume_ave_xalpha;
    double volume_ave_Xheat;
    double volume_ave_Xion;
    double volume_ave_TS;
    double volume_ave_TK;
    double volume_ave_xe;
    double volume_ave_Tb;

    int started;
    int finished;
    int buffer_size;
} reion_grids_t;

//! The meraxes halo structure
typedef struct halo_t {
    struct fof_group_t* FOFGroup;
    struct halo_t* NextHaloInFOFGroup;
    struct galaxy_t* Galaxy;

    float Pos[3]; //!< Most bound particle position [Mpc/h]
    float Vel[3]; //!< Centre of mass velocity [Mpc/h]
    float AngMom[3]; //!< Specific angular momentum vector [Mpc/h *km/s]

    double Mvir; //!< virial mass [M_sol/h]
    double Rvir; //!< Virial radius [Mpc/h]
    double Vvir; //!< Virial velocity [km/s]

    float Vmax; //!< Maximum circular velocity [km/s]
    unsigned long ID; //!< Halo ID
    int Type; //!< Type (0 for central, 1 for satellite)
    int SnapOffset; //!< Number of snapshots this halo skips before reappearing
    int DescIndex; //!< Index of descendant in next relevant snapshot
    int ProgIndex; //!< Index of progenitor in previous relevant snapshot
    int TreeFlags; //!< Bitwise flag indicating the type of match in the trees
    int Len; //!< Number of particles in the structure
} halo_t;

typedef struct fof_group_t {
    halo_t* FirstHalo;
    halo_t* FirstOccupiedHalo;
    double Mvir;
    double Rvir;
    double Vvir;
    double FOFMvirModifier;
    int TotalSubhaloLen;
} fof_group_t;

typedef struct galaxy_t {
    double NewStars[N_HISTORY_SNAPS];
    double NewMetals[N_HISTORY_SNAPS];

    #ifdef CALC_MAGS
    double inBCFlux[MAGS_N];
    double outBCFlux[MAGS_N];
    #endif

    // Unique ID for the galaxy
    unsigned long ID;

    // properties of subhalo at the last time this galaxy was a central galaxy
    float Pos[3];
    float Vel[3];
    double Mvir;
    double Rvir;
    double Vvir;
    double Vmax;
    double Spin;

    double dt; //!< Time between current snapshot and last identification

    struct halo_t* Halo;
    struct galaxy_t* FirstGalInHalo;
    struct galaxy_t* NextGalInHalo;
    struct galaxy_t* Next;
    struct galaxy_t* MergerTarget;

    // baryonic reservoirs
    double HotGas;
    double MetalsHotGas;
    double ColdGas;
    double MetalsColdGas;
    double H2Frac;
    double H2Mass;
    double HIMass;
    double Mcool;
    double StellarMass;
    double GrossStellarMass;
    double Fesc;
    double FescWeightedGSM;
    double MetalsStellarMass;
    double DiskScaleLength;
    double Sfr;
    double EjectedGas;
    double MetalsEjectedGas;
    double BlackHoleMass;
    double FescBH;
    double BHemissivity;
    double EffectiveBHM;
    double BlackHoleAccretedHotMass;
    double BlackHoleAccretedColdMass;
    double BlackHoleAccretingColdMass;

    // baryonic hostories
    double mwmsa_num;
    double mwmsa_denom;

    // misc
    double Rcool;
    double Cos_Inc;
    double MergTime;
    double MergerStartRadius;
    double BaryonFracModifier;
    double FOFMvirModifier;
    double MvirCrit;
    double MergerBurstMass;

    int Type;
    int OldType;
    int Len;
    int MaxLen;
    int SnapSkipCounter;
    int HaloDescIndex;
    int TreeFlags;
    int LastIdentSnap; //!< Last snapshot at which the halo in which this galaxy resides was identified
    int output_index; //!< write index

    bool ghost_flag;

    // N.B. There will be padding present at the end of this struct, but amount
    // is dependent on CALC_MAGS, MAX_PHOTO_NBANDS, NOUT and N_HISTORY_SNAPS.
} galaxy_t;

typedef struct galaxy_output_t {
    long long HaloID;

    // Unique ID for the galaxy
    unsigned long ID;

#ifdef CALC_MAGS
    float Mags[MAGS_N_BANDS];
#endif

    int Type;
    int CentralGal;
    int GhostFlag;
    int Len;
    int MaxLen;

    float Pos[3];
    float Vel[3];
    float Spin;
    float Mvir;
    float Rvir;
    float Vvir;
    float Vmax;
    float FOFMvir;

    // baryonic reservoirs
    float HotGas;
    float MetalsHotGas;
    float ColdGas;
    float MetalsColdGas;
    float H2Frac;
    float H2Mass;
    float HIMass;
    float Mcool;
    float DiskScaleLength;
    float StellarMass;
    float GrossStellarMass;
    float Fesc;
    float FescWeightedGSM;
    float MetalsStellarMass;
    float Sfr;
    float EjectedGas;
    float MetalsEjectedGas;
    float BlackHoleMass;
    float FescBH;
    float BHemissivity;
    float EffectiveBHM;
    float BlackHoleAccretedHotMass;
    float BlackHoleAccretedColdMass;

    // misc
    float Rcool;
    float Cos_Inc;
    float MergTime;
    float MergerStartRadius;
    float BaryonFracModifier;
    float FOFMvirModifier;
    float MvirCrit;
    float dt;
    float MergerBurstMass;

    // baryonic histories
    float MWMSA; // Mass weighted mean stellar age
    float NewStars[N_HISTORY_SNAPS];
} galaxy_output_t;

//! Tree info struct
typedef struct trees_info_t {
    int n_halos;
    int n_halos_max;
    int max_tree_id;
    int n_fof_groups;
    int n_fof_groups_max;
} trees_info_t;

typedef struct Modifier {
    float logMmin;
    float logMmax;
    float mass_mean;
    float mass_errl;
    float mass_erru;
    float ratio;
    float ratio_errl;
    float ratio_erru;
} Modifier;

// This structure carries the information about
//   the GPU allocated to this CPU's scope. It
//   needs to be declared by all compilers since
//   it is used by run_globals.
#ifdef USE_CUDA
#include <cuda_runtime.h>
typedef struct gpu_info{
    int    device;                    // the ordinal of the current context's device
    bool   flag_use_cuFFT;            // true if the code has been compiled with cuFFT
    struct cudaDeviceProp properties; // Properties of this context's assigned device
    int    n_threads;                 // No. of threads to use in kernal calls
    int    n_contexts;                // No. of ranks with successfully allocated GPU contexts
} gpu_info;
#else
typedef char gpu_info;
#endif

#ifdef CALC_MAGS
typedef struct mag_params_t {
    int targetSnap[MAGS_N_SNAPS];
    int nBeta;
    int nRest;
    int minZ;
    int maxZ;
    int nMaxZ;
    double tBC;
    int iAgeBC[MAGS_N_SNAPS];
    size_t totalSize;
    double *working;
    double *inBC;
    double *outBC;
    double *centreWaves;
    double *logWaves;
} mag_params_t;
#endif

//! Global variables which will will be passed around
typedef struct run_globals_t {
    struct run_params_t params;
    char FNameOut[STRLEN];
    reion_grids_t reion_grids;
    struct run_units_t units;
    hdf5_output_t hdf5props;

    MPI_Comm mpi_comm;
    int mpi_rank;
    int mpi_size;
    gpu_info *gpu;

    double* AA;
    double* ZZ;
    double* LTTime;
    long* RequestedForestId;
    int RequestedMassRatioModifier;
    int RequestedBaryonFracModifier;
    int* ListOutputSnaps;
    halo_t** SnapshotHalo;
    fof_group_t** SnapshotFOFGroup;
    int** SnapshotIndexLookup;
    float** SnapshotDeltax;
    float** SnapshotVel;
    trees_info_t* SnapshotTreesInfo;
    struct galaxy_t* FirstGal;
    struct galaxy_t* LastGal;
    gsl_rng* random_generator;
    void* mhysa_self;
    double Hubble;
    double RhoCrit;
    double G;
    double Csquare;

    #ifdef CALC_MAGS
    struct mag_params_t mag_params;
    #endif

    int NOutputSnaps;
    int LastOutputSnap;
    int NGhosts;
    int NHalosMax;
    int NFOFGroupsMax;
    int NRequestedForests;
    int NStoreSnapshots;

    bool SelectForestsSwitch;
    Modifier* mass_ratio_modifier;
    Modifier* baryon_frac_modifier;
} run_globals_t;
#ifdef _MAIN
run_globals_t run_globals;
#else
extern run_globals_t run_globals;
#endif

/*
 * Functions
 */

#ifdef __cplusplus
extern "C" {
#endif
void cleanup(void);
void read_parameter_file(char* fname, int mode);
void init_storage(void);
void init_meraxes(void);
void set_units(void);
void continue_prompt(char* param_file);

trees_info_t read_halos(int snapshot, halo_t** halo, fof_group_t** fof_group, int** index_lookup, trees_info_t* snapshot_trees_info);

void read_trees__velociraptor(int snapshot, halo_t* halos, int* n_halos, fof_group_t* fof_groups, int* n_fof_groups, int* index_lookup);
trees_info_t read_trees_info__velociraptor(const int snapshot);

void read_trees__gbptrees(int snapshot, halo_t* halo, int n_halos, fof_group_t* fof_group, int n_fof_groups, int n_requested_forests, int* n_halos_kept, int* n_fof_groups_kept, int* index_lookup);
trees_info_t read_trees_info__gbptrees(int snapshot);

void free_halo_storage(void);
void initialize_halo_storage(void);

void dracarys(void);
int evolve_galaxies(fof_group_t* fof_group, int snapshot, int NGal, int NFof);
void passively_evolve_ghost(galaxy_t* gal, int snapshot);
galaxy_t* new_galaxy(int snapshot, unsigned long halo_ID);
void create_new_galaxy(int snapshot, halo_t* halo, int* NGal, int* new_gal_counter, int* merger_counter);
void kill_galaxy(galaxy_t* gal, galaxy_t* prev_gal, int* NGal, int* kill_counter);
void copy_halo_props_to_galaxy(halo_t* halo, galaxy_t* gal);
void connect_galaxy_and_halo(galaxy_t* gal, halo_t* halo, int* merger_counter);
void reset_galaxy_properties(galaxy_t* gal, int snapshot);
double gas_infall(fof_group_t* FOFgroup, int snapshot);
void add_infall_to_hot(galaxy_t* central, double infall_mass);
double calculate_merging_time(galaxy_t* gal, int snapshot);
void merge_with_target(galaxy_t* gal, int* dead_gals, int snapshot);
void insitu_star_formation(galaxy_t* gal, int snapshot);
double pressure_dependent_star_formation(galaxy_t* gal, int snapshot);
void update_reservoirs_from_sf(galaxy_t* gal, double new_stars, int snapshot, SFtype type);
double sn_m_low(double log_dt);
void delayed_supernova_feedback(galaxy_t* gal, int snapshot);
void contemporaneous_supernova_feedback(galaxy_t* gal, double* m_stars, int snapshot, double* m_reheat, double* m_eject, double* m_recycled, double* new_metals);
void update_reservoirs_from_sn_feedback(galaxy_t* gal, double m_reheat, double m_eject, double m_recycled, double new_metals);
void prep_hdf5_file(void);
void create_master_file(void);
void write_snapshot(int n_write, int i_out, int* last_n_write);
void calc_hdf5_props(void);
void prepare_galaxy_for_output(galaxy_t gal, galaxy_output_t* galout, int i_snap);
void read_photometric_tables(void);
int compare_ints(const void* a, const void* b);
int compare_longs(const void* a, const void* b);
int compare_floats(const void* a, const void* b);
int compare_ptrdiff(const void* a, const void* b);
int compare_int_long(const void* a, const void* b);
int compare_slab_assign(const void* a, const void* b);
int searchsorted(void* val,
    void* arr,
    int count,
    size_t size,
    int (*compare)(const void* a, const void* b),
    int imin,
    int imax);
void timer_start(timer_info* timer);
void timer_stop(timer_info* timer);
float timer_delta(timer_info timer);
float comoving_distance(float a[3], float b[3]);
int pos_to_ngp(double x, double side, int nx);
float apply_pbc_pos(float x);
double accurate_sumf(float* arr, int n);
int grid_index(int i, int j, int k, int dim, index_type type);
void mpi_debug_here(void);
void check_mhysa_pointer(void);
int isclosef(float a, float b, float rel_tol, float abs_tol);
bool check_for_flag(int flag, int tree_flags);
int find_original_index(int index, int* lookup, int n_mappings);
void check_counts(fof_group_t* fof_group, int NGal, int NFof);
void cn_quote(void);
double Tvir_to_Mvir(double T, double z);
double hubble_at_snapshot(int snapshot);
double hubble_time(int snapshot);
double calculate_Mvir(double Mvir, int len);
double calculate_Rvir(double Mvir, int snapshot);
double calculate_Vvir(double Mvir, double Rvir);
double calculate_spin_param(halo_t* halo);
void read_mass_ratio_modifiers(int snapshot);
void read_baryon_frac_modifiers(int snapshot);
double interpolate_modifier(Modifier* modifier_data, double logM);
void read_cooling_functions(void);
double interpolate_cooling_rate(double logTemp, double logZ);
double gas_cooling(galaxy_t* gal);
void cool_gas_onto_galaxy(galaxy_t* gal, double cooling_mass);
double calc_metallicity(double total_gas, double metals);
double radio_mode_BH_heating(galaxy_t* gal, double cooling_mass, double x);
void merger_driven_BH_growth(galaxy_t* gal, double merger_ratio, int snapshot);
void previous_merger_driven_BH_growth(galaxy_t* gal);
double calculate_BHemissivity(double BlackHoleMass, double accreted_mass);
void reincorporate_ejected_gas(galaxy_t* gal);

// Numeric tools
double interp(double xp, double *x, double *y, int nPts);
double trapz_table(double *y, double *x, int nPts, double a, double b);

// Stellar feedback related
void read_stellar_feedback_tables(void);
void compute_stellar_feedback_tables(int snapshot);
double get_recycling_fraction(int i_burst, double metals);
double get_metal_yield(int i_burst, double metals);
double get_SN_energy(int i_burst, double metals);
double get_total_SN_energy(void);

// Reionization related
void read_Mcrit_table(void);
double reionization_modifier(galaxy_t* gal, double Mvir, int snapshot);
double sobacchi2013_modifier(double Mvir, double redshift);
double gnedin2000_modifer(double Mvir, double redshift);
void assign_slabs(void);
void init_reion_grids(void);

void filter(fftwf_complex* box, int local_ix_start, int slab_nx, int grid_dim, float R, int filter_type);
void set_fesc(int snapshot);
void set_quasar_fobs(void);
double RtoM(double R);
void find_HII_bubbles(int snapshot, timer_info *timer_total);
double tocf_modifier(galaxy_t* gal, double Mvir);
void update_galaxy_fesc_vals(galaxy_t *gal, double new_stars, int snapshot);
void set_ReionEfficiency(void);
int find_cell(float pos, double box_size);
void malloc_reionization_grids(void);
void free_reionization_grids(void);
int map_galaxies_to_slabs(int ngals);
void assign_Mvir_crit_to_galaxies(int ngals_in_slabs);
void construct_baryon_grids(int snapshot, int ngals);
void gen_grids_fname(const int snapshot, char* name, const bool relative);
void read_grid(const enum grid_prop property, const int snapshot, float *slab);
int read_grid__gbptrees(const enum grid_prop property, const int snapshot, float* slab);
int read_grid__velociraptor(const enum grid_prop property, const int snapshot, float* slab);
double calc_resample_factor(int n_cell[3]);
void smooth_grid(double resample_factor, int n_cell[3], fftwf_complex* slab, ptrdiff_t slab_n_complex, ptrdiff_t slab_ix_start, ptrdiff_t slab_nix);
void subsample_grid(double resample_factor, int n_cell[3], int ix_hi_start, int nix_hi, float* slab_file, float* slab);
int load_cached_slab(float* slab, int snapshot, const enum grid_prop property);
int cache_slab(float* slab, int snapshot, const enum grid_prop property);
void free_grids_cache(void);
void calculate_Mvir_crit(double redshift);
void call_find_HII_bubbles(int snapshot, int nout_gals, timer_info *timer);
void save_reion_input_grids(int snapshot);
void save_reion_output_grids(int snapshot);
bool check_if_reionization_ongoing(int snapshot);
void write_single_grid(const char* fname, float* grid, int local_ix_start, int local_nix, int dim, const char* grid_name, bool padded_flag, bool create_file_flag);

#ifdef CALC_MAGS
void init_luminosities(galaxy_t *gal);
void add_luminosities(mag_params_t *miniSpectra, galaxy_t *gal, int snapshot, double metals, double sfr);
void merge_luminosities(galaxy_t *target, galaxy_t *gal);
void init_templates_mini(mag_params_t *miniSpectra, char *fName, double *LTTime, int *targetSnaps, double *redshifts, double *betaBands, int nBeta, double *restBands, int nRest, double tBC);
void init_magnitudes(void);
void cleanup_mags(void);
void get_output_magnitudes(float *target, galaxy_t *gal, int snapshot);
#endif

// Spin temperature related
void call_ComputeTs(int snapshot, int nout_gals, timer_info* timer);
void ComputeTs(int snapshot, timer_info* timer_total);

int read_dm_vel_grid__gbptrees(int snapshot, float* slab);
void velocity_gradient(fftwf_complex* box, int local_ix_start, int slab_nx, int grid_dim);

double alpha_A(double T);
double dtdz(float z);
double drdz(float z);
double gettime(double z);
double hubble(float z);

// Brightness Temperature Box
void ComputeBrightnessTemperatureBox(int snapshot);

void Initialise_PowerSpectrum();
void Compute_PS(int snapshot);

int grid_index_smoothedSFR(int radii, int i, int j, int k, int filter_steps, int dim);
int grid_index_LC(int i, int j, int k, int dim, int dim_LC);
void Initialise_ConstructLightcone();
void ConstructLightcone(int snapshot);

// MCMC related
// meraxes_mhysa_hook must be implemented by the calling code (Mhysa)!
#ifdef _MAIN
int (*meraxes_mhysa_hook)(void *self, int snapshot, int ngals);
#else
extern  int (*meraxes_mhysa_hook)(void *self, int snapshot, int ngals);
#endif

#ifdef DEBUG
int debug(const char* restrict format, ...);
void check_pointers(halo_t* halos, fof_group_t* fof_groups, trees_info_t* trees_info);
#endif

#ifdef USE_CUDA
// This stuff is needed by the GPU routines.  This
//    needs to be included after mlog.h is included
//    and after ABORT(), myexit() & run_globals are
//    defined, since they are used within.
#include "meraxes_gpu.h"
#endif

#ifdef __cplusplus
}
#endif

#endif // _INIT_MERAXES
