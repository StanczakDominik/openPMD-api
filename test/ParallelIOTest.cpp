/* Running this test in parallel with MPI requires MPI::Init.
 * To guarantee a correct call to Init, launch the tests manually.
 */
#define CATCH_CONFIG_RUNNER

#include "openPMD/openPMD.hpp"
using namespace openPMD;

#include <catch/catch.hpp>
#if openPMD_HAVE_MPI
#   include <mpi.h>

#   include <iostream>
#   include <algorithm>
#   include <string>
#   include <vector>
#   include <array>
#   include <memory>


int main(int argc, char *argv[])
{
    MPI::Init();

    Catch::Session session;
    int result = 0;
    {
        // Indicates a command line parsing
        result = session.applyCommandLine( argc, argv );
        // RT tests
        if( result == 0 )
            result = session.run();
    }
    MPI::Finalize();
    return result;
}
#else
int main(int argc, char *argv[])
{
    Catch::Session session;
    int result = 0;
    {
        // Indicates a command line parsing
        result = session.applyCommandLine( argc, argv );
        // RT tests
        if( result == 0 )
            result = session.run();
    }
    return result;
}

TEST_CASE( "none", "[parallel]" )
{ }
#endif

#if openPMD_HAVE_HDF5 && openPMD_HAVE_MPI
TEST_CASE( "git_hdf5_sample_content_test", "[parallel][hdf5]" )
{
    int mpi_rank{-1};
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    /* only a 3x3x3 chunk of the actual data is hardcoded. every worker reads 1/3 */
    uint64_t rank = mpi_rank % 3;
    try
    {
        Series o = Series("../samples/git-sample/data00000%T.h5", AccessType::READ_ONLY, MPI_COMM_WORLD);

        {
            double actual[3][3][3] = {{{-1.9080703683727052e-09, -1.5632650729457964e-10, 1.1497536256399599e-09},
                                       {-1.9979540244463578e-09, -2.5512036927466397e-10, 1.0402234629225404e-09},
                                       {-1.7353589676361025e-09, -8.0899198451334087e-10, -1.6443779671249104e-10}},

                                      {{-2.0029988778702545e-09, -1.9543477947081556e-10, 1.0916454407094989e-09},
                                       {-2.3890367462087170e-09, -4.7158010829662089e-10, 9.0026075483251589e-10},
                                       {-1.9033881137886510e-09, -7.5192119197708962e-10, 5.0038861942880430e-10}},

                                      {{-1.3271805876513554e-09, -5.9243276950837753e-10, -2.2445734160214670e-10},
                                       {-7.4578609954301101e-10, -1.1995737736469891e-10, 2.5611823772919706e-10},
                                       {-9.4806251738077663e-10, -1.5472800818372434e-10, -3.6461900165818406e-10}}};
            MeshRecordComponent& rho = o.iterations[100].meshes["rho"][MeshRecordComponent::SCALAR];
            Offset offset{20 + rank, 20, 190};
            Extent extent{1, 3, 3};
            auto data = rho.loadChunk<double>(offset, extent);
            o.flush();
            double* raw_ptr = data.get();

            for( int j = 0; j < 3; ++j )
                for( int k = 0; k < 3; ++k )
                    REQUIRE(raw_ptr[j*3 + k] == actual[rank][j][k]);
        }

        {
            double constant_value = 9.1093829099999999e-31;
            RecordComponent& electrons_mass = o.iterations[100].particles["electrons"]["mass"][RecordComponent::SCALAR];
            Offset offset{(rank+1) * 5};
            Extent extent{3};
            auto data = electrons_mass.loadChunk<double>(offset, extent);
            o.flush();
            double* raw_ptr = data.get();

            for( int i = 0; i < 3; ++i )
                REQUIRE(raw_ptr[i] == constant_value);
        }
    } catch (no_such_file_error& e)
    {
        std::cerr << "git sample not accessible. (" << e.what() << ")\n";
        return;
    }
}

TEST_CASE( "hdf5_write_test", "[parallel][hdf5]" )
{
    int mpi_s{-1};
    int mpi_r{-1};
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_s);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_r);
    uint64_t mpi_size = static_cast<uint64_t>(mpi_s);
    uint64_t mpi_rank = static_cast<uint64_t>(mpi_r);
    Series o = Series("../samples/parallel_write.h5", AccessType::CREATE, MPI_COMM_WORLD);

    o.setAuthor("Parallel HDF5");
    ParticleSpecies& e = o.iterations[1].particles["e"];

    std::vector< double > position_global(mpi_size);
    double pos{0.};
    std::generate(position_global.begin(), position_global.end(), [&pos]{ return pos++; });
    std::shared_ptr< double > position_local(new double);
    *position_local = position_global[mpi_rank];

    e["position"]["x"].resetDataset(Dataset(determineDatatype(position_local), {mpi_size}));
    e["position"]["x"].storeChunk({mpi_rank}, {1}, position_local);

    std::vector< uint64_t > positionOffset_global(mpi_size);
    uint64_t posOff{0};
    std::generate(positionOffset_global.begin(), positionOffset_global.end(), [&posOff]{ return posOff++; });
    std::shared_ptr< uint64_t > positionOffset_local(new uint64_t);
    *positionOffset_local = positionOffset_global[mpi_rank];

    e["positionOffset"]["x"].resetDataset(Dataset(determineDatatype(positionOffset_local), {mpi_size}));
    e["positionOffset"]["x"].storeChunk({mpi_rank}, {1}, positionOffset_local);

    o.flush();
}
#else
TEST_CASE( "no_parallel_hdf5", "[parallel][hdf5]" )
{
    REQUIRE(true);
}
#endif
#if openPMD_HAVE_ADIOS1 && openPMD_HAVE_MPI
TEST_CASE( "adios_write_test", "[parallel][adios]" )
{
    Series o = Series("../samples/parallel_write.bp", AccessType::CREATE, MPI_COMM_WORLD);

    int size{-1};
    int rank{-1};
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    uint64_t mpi_size = static_cast<uint64_t>(size);
    uint64_t mpi_rank = static_cast<uint64_t>(rank);

    o.setAuthor("Parallel ADIOS1");
    ParticleSpecies& e = o.iterations[1].particles["e"];

    std::vector< double > position_global(mpi_size);
    double pos{0.};
    std::generate(position_global.begin(), position_global.end(), [&pos]{ return pos++; });
    std::shared_ptr< double > position_local(new double);
    *position_local = position_global[mpi_rank];

    e["position"]["x"].resetDataset(Dataset(determineDatatype(position_local), {mpi_size}));
    e["position"]["x"].storeChunk({mpi_rank}, {1}, position_local);

    std::vector< uint64_t > positionOffset_global(mpi_size);
    uint64_t posOff{0};
    std::generate(positionOffset_global.begin(), positionOffset_global.end(), [&posOff]{ return posOff++; });
    std::shared_ptr< uint64_t > positionOffset_local(new uint64_t);
    *positionOffset_local = positionOffset_global[mpi_rank];

    e["positionOffset"]["x"].resetDataset(Dataset(determineDatatype(positionOffset_local), {mpi_size}));
    e["positionOffset"]["x"].storeChunk({mpi_rank}, {1}, positionOffset_local);

    o.flush();
}

TEST_CASE( "hzdr_adios_sample_content_test", "[parallel][adios1]" )
{
    int mpi_rank{-1};
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    /* only a 3x3x3 chunk of the actual data is hardcoded. every worker reads 1/3 */
    uint64_t rank = mpi_rank % 3;
    try
    {
        /* development/huebl/lwfa-bgfield-001 */
        Series o = Series("../samples/hzdr-sample/bp/checkpoint_%T.bp", AccessType::READ_ONLY, MPI_COMM_WORLD);

        if( o.iterations.count(0) == 1)
        {
            float actual[3][3][3] = {{{6.7173387e-06f, 6.7173387e-06f, 6.7173387e-06f},
                                         {7.0438218e-06f, 7.0438218e-06f, 7.0438218e-06f},
                                         {7.3689453e-06f, 7.3689453e-06f, 7.3689453e-06f}},
                                     {{6.7173387e-06f, 6.7173387e-06f, 6.7173387e-06f},
                                         {7.0438218e-06f, 7.0438218e-06f, 7.0438218e-06f},
                                         {7.3689453e-06f, 7.3689453e-06f, 7.3689453e-06f}},
                                     {{6.7173387e-06f, 6.7173387e-06f, 6.7173387e-06f},
                                         {7.0438218e-06f, 7.0438218e-06f, 7.0438218e-06f},
                                         {7.3689453e-06f, 7.3689453e-06f, 7.3689453e-06f}}};

            MeshRecordComponent& B_z = o.iterations[0].meshes["B"]["z"];

            Offset offset{20 + rank, 20, 150};
            Extent extent{1, 3, 3};
            auto data = B_z.loadChunk<float>(offset, extent);
            o.flush();
            float* raw_ptr = data.get();

            for( int j = 0; j < 3; ++j )
                for( int k = 0; k < 3; ++k )
                    REQUIRE(raw_ptr[j*3 + k] == actual[rank][j][k]);
        }
    } catch (no_such_file_error& e)
    {
        std::cerr << "git sample not accessible. (" << e.what() << ")\n";
        return;
    }

}
#endif
