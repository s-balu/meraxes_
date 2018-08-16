#include "meraxes.h"
#include <gsl/gsl_integration.h>
#include <gsl/gsl_math.h>
#include <time.h>

void init_gpu()
{
// If we are compiling with CUDA, allocate a structure
//   that will carry information about the device
#ifdef USE_CUDA
    // Alocate the structure that will carry all the information
    //   about the GPU assigned to this thread
    run_globals.gpu = (gpu_info*)malloc(sizeof(gpu_info));

    // This function has all the CUDA device polling calls
    init_CUDA();

#ifdef USE_CUFFT
    // At present, Meraxes can only use cuFFT when one MPI rank
    //   is involved.  To allow this to be multicore, the code
    //   which handles the interpolation of the grids for the
    //   galaxies will need to be adjusted.
    if (run_globals.mpi_size > 1) {
        mlog_error("cuFFT is not yet supported for mpi_size>1.");
        ABORT(EXIT_FAILURE);
    }

    run_globals.gpu->flag_use_cuFFT = true;
#else
    run_globals.gpu->flag_use_cuFFT = false;
#endif

// If we are not compiling with CUDA, set this
//   pointer to NULL.  This is a good way
//   to test in the code if a GPU is being used.
#else
    mlog("CPU-only version of Meraxes running.", MLOG_MESG);
    run_globals.gpu = NULL;
#endif
}

static void read_requested_forest_ids()
{
    if (strlen(run_globals.params.ForestIDFile) == 0) {
        run_globals.NRequestedForests = -1;
        run_globals.RequestedForestId = NULL;
        return;
    }

    if (run_globals.mpi_rank == 0) {
        FILE* fin;
        char* line = NULL;
        size_t len;
        int n_forests = -1;
        int* ids;

        if (!(fin = fopen(run_globals.params.ForestIDFile, "r"))) {
            mlog_error("Failed to open file: %s", run_globals.params.ForestIDFile);
            ABORT(EXIT_FAILURE);
        }

        getline(&line, &len, fin);
        n_forests = atoi(line);
        run_globals.NRequestedForests = n_forests;

        run_globals.RequestedForestId = malloc(sizeof(int) * n_forests);
        ids = run_globals.RequestedForestId;

        for (int ii = 0; ii < n_forests; ii++) {
            getline(&line, &len, fin);
            ids[ii] = atoi(line);
        }

        free(line);

        mlog("Found %d requested forest IDs", MLOG_MESG, n_forests);

        fclose(fin);
    }

    // broadcast the data to all other ranks
    MPI_Bcast(&(run_globals.NRequestedForests), 1, MPI_INT, 0, run_globals.mpi_comm);
    if (run_globals.mpi_rank > 0)
        run_globals.RequestedForestId = malloc(sizeof(int) * run_globals.NRequestedForests);
    MPI_Bcast(run_globals.RequestedForestId, run_globals.NRequestedForests, MPI_INT, 0, run_globals.mpi_comm);
}

static void read_snap_list()
{
    if (run_globals.mpi_rank == 0) {
        FILE* fin;
        int snaplist_len;
        double dummy;
        char fname[STRLEN];
        run_params_t params = run_globals.params;

        sprintf(fname, "%s/a_list.txt", params.SimulationDir);

        if (!(fin = fopen(fname, "r"))) {
            mlog_error("failed to read snaplist in file '%s'", fname);
            ABORT(EXIT_FAILURE);
        }

        // Count the number of snapshot list entries
        snaplist_len = 0;
        do {
            if (fscanf(fin, " %lg ", &dummy) == 1)
                snaplist_len++;
            else
                break;
        } while (true);

        run_globals.params.SnaplistLength = snaplist_len;
        if (run_globals.mpi_rank == 0)
            printf("found %d defined times in snaplist.\n", snaplist_len);

        // malloc the relevant arrays
        run_globals.AA = malloc(sizeof(double) * snaplist_len);
        run_globals.ZZ = malloc(sizeof(double) * snaplist_len);
        run_globals.LTTime = malloc(sizeof(double) * snaplist_len);

        // seek back to the start of the file
        rewind(fin);

        // actually read in the expansion factors
        snaplist_len = 0;
        do {
            if (fscanf(fin, " %lg ", &(run_globals.AA[snaplist_len])) == 1)
                snaplist_len++;
            else
                break;
        } while (true);

        // close the file
        fclose(fin);
    }

    // broadcast the read to all other ranks and malloc the necessary arrays
    MPI_Bcast(&(run_globals.params.SnaplistLength), 1, MPI_INT, 0, run_globals.mpi_comm);
    if (run_globals.mpi_rank > 0) {
        run_globals.AA = malloc(sizeof(double) * run_globals.params.SnaplistLength);
        run_globals.ZZ = malloc(sizeof(double) * run_globals.params.SnaplistLength);
        run_globals.LTTime = malloc(sizeof(double) * run_globals.params.SnaplistLength);
    }
    MPI_Bcast(run_globals.AA, run_globals.params.SnaplistLength, MPI_DOUBLE, 0, run_globals.mpi_comm);
}

double integrand_time_to_present(double a, void* params)
{
    double omega_m = ((run_params_t*)params)->OmegaM;
    double omega_k = ((run_params_t*)params)->OmegaK;
    double omega_lambda = ((run_params_t*)params)->OmegaLambda;

    return 1 / sqrt(omega_m / a + omega_k + omega_lambda * a * a);
}

static double time_to_present(double z)
{
#define WORKSIZE 1000
    gsl_function F;
    gsl_integration_workspace* workspace;
    double time;
    double result;
    double abserr;

    workspace = gsl_integration_workspace_alloc(WORKSIZE);
    F.function = &integrand_time_to_present;
    F.params = &(run_globals.params);

    gsl_integration_qag(&F, 1.0 / (z + 1), 1.0, 1.0 / run_globals.Hubble,
        1.0e-8, WORKSIZE, GSL_INTEG_GAUSS21, workspace, &result, &abserr);

    time = 1 / run_globals.Hubble * result;

    gsl_integration_workspace_free(workspace);

    // return time to present as a function of redshift
    return time;
}

void set_units()
{
    run_units_t* units = &(run_globals.units);

    units->UnitTime_in_s = units->UnitLength_in_cm / units->UnitVelocity_in_cm_per_s;
    units->UnitTime_in_Megayears = units->UnitTime_in_s / SEC_PER_MEGAYEAR;

    run_globals.G = GRAVITY / pow(units->UnitLength_in_cm, 3) * units->UnitMass_in_g * pow(units->UnitTime_in_s, 2);
    run_globals.Csquare = pow(C / units->UnitVelocity_in_cm_per_s, 2);

    units->UnitDensity_in_cgs = units->UnitMass_in_g / pow(units->UnitLength_in_cm, 3);
    units->UnitPressure_in_cgs = units->UnitMass_in_g / units->UnitLength_in_cm / pow(units->UnitTime_in_s, 2);
    units->UnitCoolingRate_in_cgs = units->UnitPressure_in_cgs / units->UnitTime_in_s;

    units->UnitEnergy_in_cgs = units->UnitMass_in_g * pow(units->UnitLength_in_cm, 2) / pow(units->UnitTime_in_s, 2);

    // convert some physical input parameters to internal units
    run_globals.Hubble = HUBBLE * units->UnitTime_in_s;

    // compute a few quantitites
    run_globals.RhoCrit = 3 * run_globals.Hubble * run_globals.Hubble / (8 * M_PI * run_globals.G);

    // debug("UnitTime_in_s = %e\nUnitTime_in_Megayears = %e\nG = %e\nUnitDensity_in_cgs = %e\nUnitPressure_in_cgs = %e\nUnitCoolingRate_in_cgs = %e\nUnitEnergy_in_cgs = %e\n",
    //     units->UnitTime_in_s, units->UnitTime_in_Megayears, units->UnitDensity_in_cgs, units->UnitPressure_in_cgs, units->UnitCoolingRate_in_cgs, units->UnitEnergy_in_cgs);
    // ABORT(EXIT_SUCCESS);
}

static void read_output_snaps()
{
    int** ListOutputSnaps = &(run_globals.ListOutputSnaps);
    int* LastOutputSnap = &(run_globals.LastOutputSnap);
    int maxsnaps = run_globals.params.SnaplistLength;
    int* nout = &(run_globals.NOutputSnaps);

    if (run_globals.mpi_rank == 0) {
        int i;
        char fname[STRLEN];
        FILE* fd;

        strcpy(fname, run_globals.params.FileWithOutputSnaps);

        if (!(fd = fopen(fname, "r"))) {
            mlog_error("file `%s' not found.", fname);
            exit(EXIT_FAILURE);
        }

        // find out how many output snapshots are being requested
        int dummy = 0;
        *nout = 0;
        for (i = 0; i < maxsnaps; i++) {
            if (fscanf(fd, " %d ", &dummy) == 1)
                (*nout)++;
            else
                break;
        }
        fseek(fd, 0, SEEK_SET);

        // allocate the ListOutputSnaps array
        *ListOutputSnaps = malloc(sizeof(int) * (*nout));

        for (i = 0; i < (*nout); i++)
            if (fscanf(fd, " %d ", &((*ListOutputSnaps)[i])) != 1) {
                mlog_error("I/O error in file '%s'\n", fname);
                exit(EXIT_FAILURE);
            }
        fclose(fd);

#ifdef CALC_MAGS
        if (*nout != NOUT) {
            mlog_error("Number of entries in output snaplist does not match NOUT!");
            ABORT(EXIT_FAILURE);
        }
#endif

        // Loop through the read in snapshot numbers and convert any negative
        // values to positive ones ala python indexing conventions...
        // e.g. -1 -> MAXSNAPS-1 and so on...
        // Also store the last requested output snapnum
        *LastOutputSnap = 0;
        for (i = 0; i < (*nout); i++) {
            if ((*ListOutputSnaps)[i] < 0)
                (*ListOutputSnaps)[i] += run_globals.params.SnaplistLength;
            if ((*ListOutputSnaps)[i] > *LastOutputSnap)
                *LastOutputSnap = (*ListOutputSnaps)[i];
        }

        // sort the list from low to high snapnum
        qsort(*ListOutputSnaps, (*nout), sizeof(int), compare_ints);
    }

    // broadcast the data to all other ranks
    MPI_Bcast(nout, 1, MPI_INT, 0, run_globals.mpi_comm);

    if (run_globals.mpi_rank > 0)
        *ListOutputSnaps = malloc(sizeof(int) * (*nout));

    MPI_Bcast(*ListOutputSnaps, *nout, MPI_INT, 0, run_globals.mpi_comm);
    MPI_Bcast(LastOutputSnap, 1, MPI_INT, 0, run_globals.mpi_comm);
}

static double find_min_dt(const int n_history_snaps)
{
    double* LTTime = run_globals.LTTime;
    int n_snaps = run_globals.params.SnaplistLength;
    double min_dt = LTTime[0] - LTTime[n_snaps - 1];

    for (int ii = 0; ii < n_snaps - n_history_snaps; ii++) {
        double diff = LTTime[ii] - LTTime[ii + n_history_snaps];
        if (diff < min_dt)
            min_dt = diff;
    }

    min_dt *= run_globals.units.UnitTime_in_Megayears / run_globals.params.Hubble_h;

    return min_dt;
}

static double least_massive_stars_tracked(const int n_history_snaps)
{
    // Check that n_history_snaps is set to a high enough value to allow all
    // SN-II to be tracked across the entire simulation.  This is calculated in
    // an extremely crude fasion!
    double min_dt = find_min_dt(n_history_snaps);
    double m_low = sn_m_low(log10(min_dt));

    return m_low;
}

void init_meraxes()
{
    int i;
    int snaplist_len;

    // initialize GPU
    init_gpu();

    // initialise the random number generator
    run_globals.random_generator = gsl_rng_alloc(gsl_rng_ranlxd1);
    gsl_rng_set(run_globals.random_generator, run_globals.params.RandomSeed);

    // set the units
    set_units();

    // init the stdlib random number generator (for CN exceptions only)
    srand((unsigned)time(NULL));

    // read the input snaps list
    read_snap_list();

    // read the output snap list
    read_output_snaps();

    snaplist_len = run_globals.params.SnaplistLength;
    for (i = 0; i < snaplist_len; i++) {
        run_globals.ZZ[i] = 1 / run_globals.AA[i] - 1;
        run_globals.LTTime[i] = time_to_present(run_globals.ZZ[i]);
    }

    // check to ensure N_HISTORY_SNAPS is set to a high enough value
    double m_low = least_massive_stars_tracked(N_HISTORY_SNAPS);
    if (m_low > 8.0) {
        mlog_error("N_HISTORY_SNAPS is likely not set to a high enough value!  Exiting...");
        ABORT(EXIT_FAILURE);
    }

    // read in the requested forest IDs (if any)
    read_requested_forest_ids();

    // read in the photometric tables if required
    read_photometric_tables();

    // read in the cooling functions
    read_cooling_functions();

    // set RequestedMassRatioModifier and RequestedBaryonFracModifieruto be 1 first
    // it will be set to -1 later if MassRatioModifier or BaryonFracModifier is not specified
    run_globals.RequestedMassRatioModifier = 1;
    run_globals.RequestedBaryonFracModifier = 1;

    // read in the mean Mvir_crit table (if needed)
    read_Mcrit_table();

    // Initialise galaxy pointers
    run_globals.FirstGal = NULL;
    run_globals.LastGal = NULL;

    // Set the SelectForestsSwitch
    run_globals.SelectForestsSwitch = true;

    // This will be set by Mhysa
    run_globals.mhysa_self = NULL;

    // Initialize the halo storage arrays
    initialize_halo_storage();

    // Determine the size of the light-cone for initialising the light-cone grid
    if(run_globals.params.Flag_PatchyReion && run_globals.params.Flag_ConstructLightcone) {
        Initialise_ConstructLightcone();
    }

    malloc_reionization_grids();
    set_ReionEfficiency();
    set_quasar_fobs();

    // calculate the output hdf5 file properties for later use
    calc_hdf5_props();
}
