
#include "block2_core.hpp"
#include "block2_dmrg.hpp"
#include <gtest/gtest.h>

using namespace block2;

// suppress googletest output for non-root mpi procs
struct MPITest {
    shared_ptr<testing::TestEventListener> tel;
    testing::TestEventListener *def_tel;
    MPITest() {
        if (block2::MPI::rank() != 0) {
            testing::TestEventListeners &tels =
                testing::UnitTest::GetInstance()->listeners();
            def_tel = tels.Release(tels.default_result_printer());
            tel = make_shared<testing::EmptyTestEventListener>();
            tels.Append(tel.get());
        }
    }
    ~MPITest() {
        if (block2::MPI::rank() != 0) {
            testing::TestEventListeners &tels =
                testing::UnitTest::GetInstance()->listeners();
            assert(tel.get() == tels.Release(tel.get()));
            tel = nullptr;
            tels.Append(def_tel);
        }
    }
    static bool okay() {
        static MPITest _mpi_test;
        return _mpi_test.tel != nullptr;
    }
};

class TestNPDM : public ::testing::Test {
    static bool _mpi;

  protected:
    size_t isize = 1L << 30;
    size_t dsize = 1L << 34;
    void SetUp() override {
        Random::rand_seed(0);
        frame_() = make_shared<DataFrame>(isize, dsize, "nodex");
        threading_() = make_shared<Threading>(
            ThreadingTypes::OperatorBatchedGEMM | ThreadingTypes::Global, 4, 4,
            4);
        threading_()->seq_type = SeqTypes::Simple;
        cout << *threading_() << endl;
    }
    void TearDown() override {
        frame_()->activate(0);
        assert(ialloc_()->used == 0 && dalloc_()->used == 0);
        frame_() = nullptr;
    }
};

bool TestNPDM::_mpi = MPITest::okay();

void load_twopdm(const string &filename_2pdm,
                 vector<tuple<int, int, int, int, double>> *two_pdm) {
    ifstream ifs(filename_2pdm.c_str());
    ASSERT_TRUE(ifs.good());
    vector<string> lines = Parsing::readlines(&ifs);
    ASSERT_FALSE(ifs.bad());
    ifs.close();
    for (auto &line : lines) {
        vector<string> items = Parsing::split(line, " ", true);
        if (items.size() == 6)
            two_pdm[Parsing::to_int(items[0])].push_back(
                make_tuple(Parsing::to_int(items[1]), Parsing::to_int(items[2]),
                           Parsing::to_int(items[3]), Parsing::to_int(items[4]),
                           Parsing::to_double(items[5])));
    }

    two_pdm[3] = two_pdm[1];
    two_pdm[1] = two_pdm[2];
    two_pdm[2] = two_pdm[3];
    two_pdm[4] = two_pdm[3];
    two_pdm[5] = two_pdm[3];
    sort(two_pdm[3].begin(), two_pdm[3].end(),
         [](const tuple<int, int, int, int, double> &i,
            const tuple<int, int, int, int, double> &j) {
             return make_tuple(get<0>(i), get<1>(i), get<3>(i), get<2>(i)) <
                    make_tuple(get<0>(j), get<1>(j), get<3>(j), get<2>(j));
         });
    sort(two_pdm[4].begin(), two_pdm[4].end(),
         [](const tuple<int, int, int, int, double> &i,
            const tuple<int, int, int, int, double> &j) {
             return make_tuple(get<1>(i), get<0>(i), get<2>(i), get<3>(i)) <
                    make_tuple(get<1>(j), get<0>(j), get<2>(j), get<3>(j));
         });
    sort(two_pdm[5].begin(), two_pdm[5].end(),
         [](const tuple<int, int, int, int, double> &i,
            const tuple<int, int, int, int, double> &j) {
             return make_tuple(get<1>(i), get<0>(i), get<3>(i), get<2>(i)) <
                    make_tuple(get<1>(j), get<0>(j), get<3>(j), get<2>(j));
         });
}

TEST_F(TestNPDM, TestSU2) {
    shared_ptr<FCIDUMP> fcidump = make_shared<FCIDUMP>();
    string filename = "data/N2.STO3G.FCIDUMP"; // E = -107.65412235
    fcidump->read(filename);
    vector<uint8_t> orbsym = fcidump->orb_sym();
    transform(orbsym.begin(), orbsym.end(), orbsym.begin(),
              PointGroup::swap_d2h);
    SU2 vacuum(0);
    SU2 target(fcidump->n_elec(), fcidump->twos(),
               PointGroup::swap_d2h(fcidump->isym()));
    int norb = fcidump->n_sites();
    shared_ptr<HamiltonianQC<SU2>> hamil = make_shared<HamiltonianQC<SU2>>(vacuum, norb, orbsym, fcidump);

#ifdef _HAS_MPI
    shared_ptr<ParallelCommunicator<SU2>> para_comm =
        make_shared<MPICommunicator<SU2>>();
#else
    shared_ptr<ParallelCommunicator<SU2>> para_comm =
        make_shared<ParallelCommunicator<SU2>>(1, 0, 0);
#endif
    shared_ptr<ParallelRule<SU2>> para_rule =
        make_shared<ParallelRuleQC<SU2>>(para_comm);
    shared_ptr<ParallelRule<SU2>> pdm1_para_rule =
        make_shared<ParallelRulePDM1QC<SU2>>(para_comm);
    shared_ptr<ParallelRule<SU2>> pdm2_para_rule =
        make_shared<ParallelRulePDM2QC<SU2>>(para_comm);

    // FCI results
    vector<tuple<int, int, double>> one_pdm = {
        {0, 0, 1.999989282592},  {0, 1, -0.000025398134},
        {0, 2, 0.000238560621},  {1, 0, -0.000025398134},
        {1, 1, 1.991431489457},  {1, 2, -0.005641787787},
        {2, 0, 0.000238560621},  {2, 1, -0.005641787787},
        {2, 2, 1.985471515555},  {3, 3, 1.999992764813},
        {3, 4, -0.000236022833}, {3, 5, 0.000163863520},
        {4, 3, -0.000236022833}, {4, 4, 1.986371259953},
        {4, 5, 0.018363506969},  {5, 3, 0.000163863520},
        {5, 4, 0.018363506969},  {5, 5, 0.019649294772},
        {6, 6, 1.931412559660},  {7, 7, 0.077134636900},
        {8, 8, 1.931412559108},  {9, 9, 0.077134637190}};

    vector<tuple<int, int, double>> one_npc_pure = {
        {0, 0, 3.999970169029}, {0, 1, 3.982843646321}, {0, 2, 3.970922563909},
        {0, 3, 3.999964132959}, {0, 4, 3.972721371453}, {0, 5, 0.039287799765},
        {0, 6, 3.862807052835}, {0, 7, 0.154263083854}, {0, 8, 3.862807051730},
        {0, 9, 0.154263084435}, {1, 0, 3.982843646321}, {1, 1, 3.976055263415},
        {1, 2, 3.954852316308}, {1, 3, 3.982848649339}, {1, 4, 3.955923388028},
        {1, 5, 0.029929030654}, {1, 6, 3.849368128446}, {1, 7, 0.149426150818},
        {1, 8, 3.849368127725}, {1, 9, 0.149426151342}, {2, 0, 3.970922563909},
        {2, 1, 3.954852316308}, {2, 2, 3.961017364056}, {2, 3, 3.970928699038},
        {2, 4, 3.944549415997}, {2, 5, 0.025738181917}, {2, 6, 3.839712263582},
        {2, 7, 0.144584074844}, {2, 8, 3.839712262429}, {2, 9, 0.144584075693},
        {3, 0, 3.999964132959}, {3, 1, 3.982848649339}, {3, 2, 3.970928699038},
        {3, 3, 3.999980830045}, {3, 4, 3.972732573769}, {3, 5, 0.039291950822},
        {3, 6, 3.862811060036}, {3, 7, 0.154264875928}, {3, 8, 3.862811058932},
        {3, 9, 0.154264876508}, {4, 0, 3.972721371453}, {4, 1, 3.955923388028},
        {4, 2, 3.944549415997}, {4, 3, 3.972732573769}, {4, 4, 3.971806112178},
        {4, 5, 0.038034034075}, {4, 6, 3.836125249965}, {4, 7, 0.140590122090},
        {4, 8, 3.836125248945}, {4, 9, 0.140590122844}, {5, 0, 0.039287799765},
        {5, 1, 0.029929030654}, {5, 2, 0.025738181917}, {5, 3, 0.039291950822},
        {5, 4, 0.038034034075}, {5, 5, 0.024837400517}, {5, 6, 0.029384912746},
        {5, 7, 0.009600951696}, {5, 8, 0.029384912710}, {5, 9, 0.009600951905},
        {6, 0, 3.862807052835}, {6, 1, 3.849368128446}, {6, 2, 3.839712263582},
        {6, 3, 3.862811060036}, {6, 4, 3.836125249965}, {6, 5, 0.029384912746},
        {6, 6, 3.834021980647}, {6, 7, 0.047095524770}, {6, 8, 3.755876421788},
        {6, 9, 0.122573240425}, {7, 0, 0.154263083854}, {7, 1, 0.149426150818},
        {7, 2, 0.144584074844}, {7, 3, 0.154264875928}, {7, 4, 0.140590122090},
        {7, 5, 0.009600951696}, {7, 6, 0.047095524770}, {7, 7, 0.125466135127},
        {7, 8, 0.122573239689}, {7, 9, 0.032020757784}, {8, 0, 3.862807051730},
        {8, 1, 3.849368127725}, {8, 2, 3.839712262429}, {8, 3, 3.862811058932},
        {8, 4, 3.836125248945}, {8, 5, 0.029384912710}, {8, 6, 3.755876421788},
        {8, 7, 0.122573239689}, {8, 8, 3.834021979234}, {8, 9, 0.047095524328},
        {9, 0, 0.154263084435}, {9, 1, 0.149426151342}, {9, 2, 0.144584075693},
        {9, 3, 0.154264876508}, {9, 4, 0.140590122844}, {9, 5, 0.009600951905},
        {9, 6, 0.122573240425}, {9, 7, 0.032020757784}, {9, 8, 0.047095524328},
        {9, 9, 0.125466135399}};

    vector<tuple<int, int, double>> one_npc_mixed = {
        {0, 0, 1.999997678747},  {0, 1, 1.991418737825},
        {0, 2, 1.985459946061},  {0, 3, 1.999982009379},
        {0, 4, 1.986360605632},  {0, 5, 0.019641047353},
        {0, 6, 1.931404467728},  {0, 7, 0.077128008976},
        {0, 8, 1.931404467175},  {0, 9, 0.077128009268},
        {1, 0, 1.991418737825},  {1, 1, 1.998239204955},
        {1, 2, 1.976076265757},  {1, 3, 1.991424179582},
        {1, 4, 1.977669816671},  {1, 5, 0.010374138663},
        {1, 6, 1.921342517811},  {1, 7, 0.076066523743},
        {1, 8, 1.921342517038},  {1, 9, 0.076066524153},
        {2, 0, 1.985459946061},  {2, 1, 1.976076265757},
        {2, 2, 1.995397182610},  {2, 3, 1.985464295413},
        {2, 4, 1.971531954721},  {2, 5, 0.000097894329},
        {2, 6, 1.920016840410},  {2, 7, 0.072119694663},
        {2, 8, 1.920016839796},  {2, 9, 0.072119695128},
        {3, 0, 1.999982009379},  {3, 1, 1.991424179582},
        {3, 2, 1.985464295413},  {3, 3, 1.999997464393},
        {3, 4, 1.986359574172},  {3, 5, 0.019645917129},
        {3, 6, 1.931405510730},  {3, 7, 0.077132446211},
        {3, 8, 1.931405510178},  {3, 9, 0.077132446501},
        {4, 0, 1.986360605632},  {4, 1, 1.977669816671},
        {4, 2, 1.971531954721},  {4, 3, 1.986359574172},
        {4, 4, 1.987307667681},  {4, 5, 0.018662092531},
        {4, 6, 1.918089954088},  {4, 7, 0.070263600112},
        {4, 8, 1.918089953582},  {4, 9, 0.070263600482},
        {5, 0, 0.019641047353},  {5, 1, 0.010374138663},
        {5, 2, 0.000097894329},  {5, 3, 0.019645917129},
        {5, 4, 0.018662092531},  {5, 5, 0.034110483799},
        {5, 6, 0.015848716008},  {5, 7, 0.001658028881},
        {5, 8, 0.015848716174},  {5, 9, 0.001658028538},
        {6, 0, 1.931404467728},  {6, 1, 1.921342517811},
        {6, 2, 1.920016840410},  {6, 3, 1.931405510730},
        {6, 4, 1.918089954088},  {6, 5, 0.015848716008},
        {6, 6, 1.960215698333},  {6, 7, -0.010381215771},
        {6, 8, 1.860365797197},  {6, 9, 0.071579631086},
        {7, 0, 0.077128008976},  {7, 1, 0.076066523743},
        {7, 2, 0.072119694663},  {7, 3, 0.077132446211},
        {7, 4, 0.070263600112},  {7, 5, 0.001658028881},
        {7, 6, -0.010381215771}, {7, 7, 0.105937775573},
        {7, 8, 0.071579630716},  {7, 9, -0.001562034805},
        {8, 0, 1.931404467175},  {8, 1, 1.921342517038},
        {8, 2, 1.920016839796},  {8, 3, 1.931405510178},
        {8, 4, 1.918089953582},  {8, 5, 0.015848716174},
        {8, 6, 1.860365797197},  {8, 7, 0.071579630716},
        {8, 8, 1.960215698089},  {8, 9, -0.010381216191},
        {9, 0, 0.077128009268},  {9, 1, 0.076066524153},
        {9, 2, 0.072119695128},  {9, 3, 0.077132446501},
        {9, 4, 0.070263600482},  {9, 5, 0.001658028538},
        {9, 6, 0.071579631086},  {9, 7, -0.001562034805},
        {9, 8, -0.010381216191}, {9, 9, 0.105937776172}};

    vector<tuple<int, int, int, int, double>> two_pdm[6];
    load_twopdm("data/N2.STO3G.2PDM", two_pdm);

    Timer t;
    t.get_time();
    // MPO construction
    cout << "MPO start" << endl;
    shared_ptr<MPO<SU2>> mpo =
        make_shared<MPOQC<SU2>>(hamil, QCTypes::Conventional);
    cout << "MPO end .. T = " << t.get_time() << endl;

    // MPO simplification
    cout << "MPO simplification start" << endl;
    mpo =
        make_shared<SimplifiedMPO<SU2>>(mpo, make_shared<RuleQC<SU2>>(), true);
    cout << "MPO simplification end .. T = " << t.get_time() << endl;

    // MPO parallelization
    cout << "MPO parallelization start" << endl;
    mpo = make_shared<ParallelMPO<SU2>>(mpo, para_rule);
    cout << "MPO parallelization end .. T = " << t.get_time() << endl;

    // 1PDM MPO construction
    cout << "1PDM MPO start" << endl;
    shared_ptr<MPO<SU2>> pmpo = make_shared<PDM1MPOQC<SU2>>(hamil);
    cout << "1PDM MPO end .. T = " << t.get_time() << endl;

    // 1PDM MPO simplification
    cout << "1PDM MPO simplification start" << endl;
    pmpo =
        make_shared<SimplifiedMPO<SU2>>(pmpo, make_shared<RuleQC<SU2>>(), true);
    cout << "1PDM MPO simplification end .. T = " << t.get_time() << endl;

    // 1PDM MPO parallelization
    cout << "1PDM MPO parallelization start" << endl;
    pmpo = make_shared<ParallelMPO<SU2>>(pmpo, pdm1_para_rule);
    cout << "1PDM MPO parallelization end .. T = " << t.get_time() << endl;

    // 2PDM MPO construction
    cout << "2PDM MPO start" << endl;
    shared_ptr<MPO<SU2>> p2mpo = make_shared<PDM2MPOQC<SU2>>(hamil);
    cout << "2PDM MPO end .. T = " << t.get_time() << endl;

    // 2PDM MPO simplification
    cout << "2PDM MPO simplification start" << endl;
    p2mpo = make_shared<SimplifiedMPO<SU2>>(p2mpo, make_shared<RuleQC<SU2>>(),
                                            true);
    cout << "2PDM MPO simplification end .. T = " << t.get_time() << endl;

    // 2PDM MPO parallelization
    cout << "2PDM MPO parallelization start" << endl;
    p2mpo = make_shared<ParallelMPO<SU2>>(p2mpo, pdm2_para_rule);
    cout << "2PDM MPO parallelization end .. T = " << t.get_time() << endl;

    // 1NPC MPO construction
    cout << "1NPC MPO start" << endl;
    shared_ptr<MPO<SU2>> nmpo = make_shared<NPC1MPOQC<SU2>>(hamil);
    cout << "1NPC MPO end .. T = " << t.get_time() << endl;

    // 1NPC MPO simplification
    cout << "1NPC MPO simplification start" << endl;
    nmpo =
        make_shared<SimplifiedMPO<SU2>>(nmpo, make_shared<Rule<SU2>>(), true);
    cout << "1NPC MPO simplification end .. T = " << t.get_time() << endl;

    // 1NPC MPO parallelization
    cout << "1NPC MPO parallelization start" << endl;
    nmpo = make_shared<ParallelMPO<SU2>>(nmpo, pdm1_para_rule);
    cout << "1NPC MPO parallelization end .. T = " << t.get_time() << endl;
    // cout << nmpo->get_blocking_formulas() << endl;
    // abort();

    ubond_t bond_dim = 200;

    for (int dot = 1; dot <= 2; dot++) {

        // MPSInfo
        shared_ptr<MPSInfo<SU2>> mps_info =
            make_shared<MPSInfo<SU2>>(norb, vacuum, target, hamil->basis);
        mps_info->set_bond_dimension(bond_dim);

        // MPS
        Random::rand_seed(0);
        shared_ptr<MPS<SU2>> mps = make_shared<MPS<SU2>>(norb, 0, dot);
        mps->initialize(mps_info);
        mps->random_canonicalize();

        // MPS/MPSInfo save mutable
        mps->save_mutable();
        mps->deallocate();
        mps_info->save_mutable();
        mps_info->deallocate_mutable();

        // ME
        shared_ptr<MovingEnvironment<SU2>> me =
            make_shared<MovingEnvironment<SU2>>(mpo, mps, mps, "DMRG");
        t.get_time();
        cout << "INIT start" << endl;
        me->init_environments(false);
        cout << "INIT end .. T = " << t.get_time() << endl;

        // DMRG
        vector<ubond_t> bdims = {bond_dim};
        vector<double> noises = {1E-8, 0};
        shared_ptr<DMRG<SU2>> dmrg = make_shared<DMRG<SU2>>(me, bdims, noises);
        dmrg->iprint = 2;
        dmrg->noise_type = NoiseTypes::ReducedPerturbativeCollected;
        dmrg->solve(10, true, 1E-12);

        // 1PDM ME
        shared_ptr<MovingEnvironment<SU2>> pme =
            make_shared<MovingEnvironment<SU2>>(pmpo, mps, mps, "1PDM");
        t.get_time();
        cout << "1PDM INIT start" << endl;
        pme->init_environments(false);
        cout << "1PDM INIT end .. T = " << t.get_time() << endl;

        // 1PDM
        shared_ptr<Expect<SU2>> expect =
            make_shared<Expect<SU2>>(pme, bond_dim, bond_dim);
        expect->solve(true, dmrg->forward);

        MatrixRef dm = expect->get_1pdm_spatial();
        int k = 0;
        for (int i = 0; i < dm.m; i++)
            for (int j = 0; j < dm.n; j++)
                if (abs(dm(i, j)) > TINY) {
                    cout << "== SU2 1PDM SPAT / " << dot
                         << "-site ==" << setw(5) << i << setw(5) << j << fixed
                         << setw(22) << fixed << setprecision(12) << dm(i, j)
                         << " error = " << scientific << setprecision(3)
                         << setw(10) << abs(dm(i, j) - get<2>(one_pdm[k]))
                         << endl;

                    EXPECT_EQ(i, get<0>(one_pdm[k]));
                    EXPECT_EQ(j, get<1>(one_pdm[k]));
                    EXPECT_LT(abs(dm(i, j) - get<2>(one_pdm[k])), 1E-6);

                    k++;
                }

        EXPECT_EQ(k, (int)one_pdm.size());

        dm.deallocate();

        dm = expect->get_1pdm();
        int kk[2] = {0, 0};
        for (int i = 0; i < dm.m; i++)
            for (int j = 0; j < dm.n; j++)
                if (abs(dm(i, j)) > TINY) {
                    EXPECT_EQ(i % 2, j % 2);
                    int ii = i / 2, jj = j / 2, p = i % 2;

                    cout << "== SU2 1PDM / " << dot << "-site ==" << setw(6)
                         << (p == 0 ? "alpha" : "beta") << setw(5) << ii
                         << setw(5) << jj << fixed << setw(22) << fixed
                         << setprecision(12) << dm(i, j)
                         << " error = " << scientific << setprecision(3)
                         << setw(10)
                         << abs(dm(i, j) - get<2>(one_pdm[kk[p]]) / 2) << endl;

                    EXPECT_EQ(ii, get<0>(one_pdm[kk[p]]));
                    EXPECT_EQ(jj, get<1>(one_pdm[kk[p]]));
                    EXPECT_LT(abs(dm(i, j) - get<2>(one_pdm[kk[p]]) / 2), 1E-6);

                    kk[p]++;
                }

        EXPECT_EQ(kk[0], (int)one_pdm.size());
        EXPECT_EQ(kk[1], (int)one_pdm.size());

        dm.deallocate();

        // 2PDM ME
        shared_ptr<MovingEnvironment<SU2>> p2me =
            make_shared<MovingEnvironment<SU2>>(p2mpo, mps, mps, "2PDM");
        t.get_time();
        cout << "2PDM INIT start" << endl;
        p2me->init_environments(false);
        cout << "2PDM INIT end .. T = " << t.get_time() << endl;

        // 2PDM
        expect = make_shared<Expect<SU2>>(p2me, bond_dim, bond_dim);
        expect->solve(true, mps->center == 0);

        int m[6] = {0, 0, 0, 0, 0, 0};
        double max_error = 0.0;
        shared_ptr<Tensor> dm2 = expect->get_2pdm();
        for (int i = 0; i < dm2->shape[0]; i++)
            for (int j = 0; j < dm2->shape[1]; j++)
                for (int k = 0; k < dm2->shape[2]; k++)
                    for (int l = 0; l < dm2->shape[3]; l++)
                        if (abs((*dm2)({i, j, k, l})) > 1E-14) {

                            int p = -1;
                            double f = 1.0;

                            int ii = i / 2, jj = j / 2, kk = k / 2, ll = l / 2;

                            if (i % 2 == 0 && j % 2 == 0 && k % 2 == 0 &&
                                l % 2 == 0)
                                p = 0, f = 1.0;
                            else if (i % 2 == 1 && j % 2 == 1 && k % 2 == 1 &&
                                     l % 2 == 1)
                                p = 1, f = 1.0;
                            else if (i % 2 == 0 && j % 2 == 1 && k % 2 == 1 &&
                                     l % 2 == 0)
                                p = 2, f = 1.0;
                            else if (i % 2 == 0 && j % 2 == 1 && k % 2 == 0 &&
                                     l % 2 == 1)
                                p = 3, f = -1.0, swap(kk, ll);
                            else if (i % 2 == 1 && j % 2 == 0 && k % 2 == 1 &&
                                     l % 2 == 0)
                                p = 4, f = -1.0, swap(ii, jj);
                            else if (i % 2 == 1 && j % 2 == 0 && k % 2 == 0 &&
                                     l % 2 == 1)
                                p = 5, f = 1.0, swap(ii, jj), swap(kk, ll);

                            EXPECT_NE(p, -1);

                            EXPECT_EQ(ii, get<0>(two_pdm[p][m[p]]));
                            EXPECT_EQ(jj, get<1>(two_pdm[p][m[p]]));
                            EXPECT_EQ(kk, get<2>(two_pdm[p][m[p]]));
                            EXPECT_EQ(ll, get<3>(two_pdm[p][m[p]]));
                            EXPECT_LT(abs((*dm2)({i, j, k, l}) -
                                          f * get<4>(two_pdm[p][m[p]])),
                                      2E-6);

                            max_error = max(max_error,
                                            abs((*dm2)({i, j, k, l}) -
                                                f * get<4>(two_pdm[p][m[p]])));

                            m[p]++;
                        }

        for (int p = 0; p < 6; p++)
            EXPECT_EQ(m[p], (int)two_pdm[p].size());

        cout << "== SU2 2PDM / " << dot << "-site =="
             << " max error = " << scientific << setprecision(3) << setw(10)
             << max_error << endl;

        m[0] = m[1] = m[2] = 0;
        max_error = 0.0;
        dm2 = expect->get_2pdm_spatial();
        for (int i = 0; i < dm2->shape[0]; i++)
            for (int j = 0; j < dm2->shape[1]; j++)
                for (int k = 0; k < dm2->shape[2]; k++)
                    for (int l = 0; l < dm2->shape[3]; l++)
                        if (abs((*dm2)({i, j, k, l})) > 1E-14) {

                            double v = 0;
                            if (m[0] < two_pdm[0].size() &&
                                two_pdm[0][m[0]] ==
                                    make_tuple(i, j, k, l,
                                               get<4>(two_pdm[0][m[0]])))
                                v += get<4>(two_pdm[0][m[0]]), m[0]++;
                            if (m[1] < two_pdm[1].size() &&
                                two_pdm[1][m[1]] ==
                                    make_tuple(i, j, k, l,
                                               get<4>(two_pdm[1][m[1]])))
                                v += get<4>(two_pdm[1][m[1]]), m[1]++;
                            if (m[2] < two_pdm[2].size() &&
                                two_pdm[2][m[2]] ==
                                    make_tuple(i, j, k, l,
                                               get<4>(two_pdm[2][m[2]])))
                                v += get<4>(two_pdm[2][m[2]]) * 2, m[2]++;

                            EXPECT_LT(abs((*dm2)({i, j, k, l}) - v), 2E-6);

                            max_error =
                                max(max_error, abs((*dm2)({i, j, k, l}) - v));
                        }

        for (int p = 0; p < 3; p++)
            EXPECT_EQ(m[p], (int)two_pdm[p].size());

        cout << "== SU2 2PDM SPAT / " << dot << "-site =="
             << " max error = " << scientific << setprecision(3) << setw(10)
             << max_error << endl;

        // 1NPC ME
        shared_ptr<MovingEnvironment<SU2>> nme =
            make_shared<MovingEnvironment<SU2>>(nmpo, mps, mps, "1NPC");
        t.get_time();
        cout << "1NPC INIT start" << endl;
        nme->init_environments(false);
        cout << "1NPC INIT end .. T = " << t.get_time() << endl;

        // 1NPC
        expect = make_shared<Expect<SU2>>(nme, bond_dim, bond_dim);
        expect->solve(true, mps->center == 0);

        MatrixRef dmx = expect->get_1npc_spatial(0);
        k = 0;
        for (int i = 0; i < dmx.m; i++)
            for (int j = 0; j < dmx.n; j++)
                if (abs(dmx(i, j)) > TINY) {
                    cout << "== SU2 1NPC  PURE / " << dot
                         << "-site ==" << setw(5) << i << setw(5) << j << fixed
                         << setw(22) << fixed << setprecision(12) << dmx(i, j)
                         << " error = " << scientific << setprecision(3)
                         << setw(10) << abs(dmx(i, j) - get<2>(one_npc_pure[k]))
                         << endl;

                    EXPECT_EQ(i, get<0>(one_npc_pure[k]));
                    EXPECT_EQ(j, get<1>(one_npc_pure[k]));
                    EXPECT_LT(abs(dmx(i, j) - get<2>(one_npc_pure[k])), 1E-6);

                    k++;
                }

        EXPECT_EQ(k, (int)one_npc_pure.size());

        dmx.deallocate();

        MatrixRef dmy = expect->get_1npc_spatial(1);
        k = 0;
        for (int i = 0; i < dmy.m; i++)
            for (int j = 0; j < dmy.n; j++)
                if (abs(dmy(i, j)) > TINY) {
                    cout << "== SU2 1NPC MIXED / " << dot
                         << "-site ==" << setw(5) << i << setw(5) << j << fixed
                         << setw(22) << fixed << setprecision(12) << dmy(i, j)
                         << " error = " << scientific << setprecision(3)
                         << setw(10)
                         << abs(dmy(i, j) - get<2>(one_npc_mixed[k])) << endl;

                    EXPECT_EQ(i, get<0>(one_npc_mixed[k]));
                    EXPECT_EQ(j, get<1>(one_npc_mixed[k]));
                    EXPECT_LT(abs(dmy(i, j) - get<2>(one_npc_mixed[k])), 1E-6);

                    k++;
                }

        EXPECT_EQ(k, (int)one_npc_mixed.size());

        dmy.deallocate();

        // deallocate persistent stack memory
        mps_info->deallocate();
    }

    nmpo->deallocate();
    pmpo->deallocate();
    mpo->deallocate();
    hamil->deallocate();
    fcidump->deallocate();
}

TEST_F(TestNPDM, TestSZ) {
    shared_ptr<FCIDUMP> fcidump = make_shared<FCIDUMP>();
    string filename = "data/N2.STO3G.FCIDUMP"; // E = -107.65412235
    fcidump->read(filename);
    vector<uint8_t> orbsym = fcidump->orb_sym();
    transform(orbsym.begin(), orbsym.end(), orbsym.begin(),
              PointGroup::swap_d2h);
    SZ vacuum(0);
    SZ target(fcidump->n_elec(), fcidump->twos(),
              PointGroup::swap_d2h(fcidump->isym()));
    int norb = fcidump->n_sites();
    shared_ptr<HamiltonianQC<SZ>> hamil = make_shared<HamiltonianQC<SZ>>(vacuum, norb, orbsym, fcidump);

#ifdef _HAS_INTEL_MKL
    mkl_set_num_threads(1);
    mkl_set_dynamic(0);
#endif

#ifdef _HAS_MPI
    shared_ptr<ParallelCommunicator<SZ>> para_comm =
        make_shared<MPICommunicator<SZ>>();
#else
    shared_ptr<ParallelCommunicator<SZ>> para_comm =
        make_shared<ParallelCommunicator<SZ>>(1, 0, 0);
#endif
    shared_ptr<ParallelRule<SZ>> para_rule =
        make_shared<ParallelRuleQC<SZ>>(para_comm);
    shared_ptr<ParallelRule<SZ>> pdm1_para_rule =
        make_shared<ParallelRulePDM1QC<SZ>>(para_comm);
    shared_ptr<ParallelRule<SZ>> pdm2_para_rule =
        make_shared<ParallelRulePDM2QC<SZ>>(para_comm);

    // FCI results
    vector<tuple<int, int, double>> one_pdm = {
        {0, 0, 1.999989282592},  {0, 1, -0.000025398134},
        {0, 2, 0.000238560621},  {1, 0, -0.000025398134},
        {1, 1, 1.991431489457},  {1, 2, -0.005641787787},
        {2, 0, 0.000238560621},  {2, 1, -0.005641787787},
        {2, 2, 1.985471515555},  {3, 3, 1.999992764813},
        {3, 4, -0.000236022833}, {3, 5, 0.000163863520},
        {4, 3, -0.000236022833}, {4, 4, 1.986371259953},
        {4, 5, 0.018363506969},  {5, 3, 0.000163863520},
        {5, 4, 0.018363506969},  {5, 5, 0.019649294772},
        {6, 6, 1.931412559660},  {7, 7, 0.077134636900},
        {8, 8, 1.931412559108},  {9, 9, 0.077134637190}};

    vector<tuple<int, int, double>> one_npc_pure = {
        {0, 0, 0.999994641296},   {0, 1, 0.999990443218},
        {0, 2, 0.995710397358},   {0, 3, 0.995711425803},
        {0, 4, 0.992730418328},   {0, 5, 0.992730863626},
        {0, 6, 0.999991023723},   {0, 7, 0.999991042757},
        {0, 8, 0.993180329514},   {0, 9, 0.993180356212},
        {0, 10, 0.009821474521},  {0, 11, 0.009822425362},
        {0, 12, 0.965701920093},  {0, 13, 0.965701606324},
        {0, 14, 0.038565182138},  {0, 15, 0.038566359788},
        {0, 16, 0.965701919817},  {0, 17, 0.965701606048},
        {0, 18, 0.038565182284},  {0, 19, 0.038566359934},
        {1, 0, 0.999990443218},   {1, 1, 0.999994641296},
        {1, 2, 0.995711425803},   {1, 3, 0.995710397358},
        {1, 4, 0.992730863626},   {1, 5, 0.992730418328},
        {1, 6, 0.999991042757},   {1, 7, 0.999991023723},
        {1, 8, 0.993180356212},   {1, 9, 0.993180329514},
        {1, 10, 0.009822425362},  {1, 11, 0.009821474521},
        {1, 12, 0.965701606324},  {1, 13, 0.965701920093},
        {1, 14, 0.038566359788},  {1, 15, 0.038565182138},
        {1, 16, 0.965701606048},  {1, 17, 0.965701919817},
        {1, 18, 0.038566359934},  {1, 19, 0.038565182284},
        {2, 0, 0.995710397358},   {2, 1, 0.995711425803},
        {2, 2, 0.995715744728},   {2, 3, 0.992311886979},
        {2, 4, 0.988488096851},   {2, 5, 0.988938061303},
        {2, 6, 0.995712138153},   {2, 7, 0.995712186516},
        {2, 8, 0.988932200868},   {2, 9, 0.989029493146},
        {2, 10, 0.006717196872},  {2, 11, 0.008247318455},
        {2, 12, 0.961785108442},  {2, 13, 0.962898955781},
        {2, 14, 0.037582110744},  {2, 15, 0.037130964665},
        {2, 16, 0.961785108201},  {2, 17, 0.962898955662},
        {2, 18, 0.037582110882},  {2, 19, 0.037130964789},
        {3, 0, 0.995711425803},   {3, 1, 0.995710397358},
        {3, 2, 0.992311886979},   {3, 3, 0.995715744728},
        {3, 4, 0.988938061303},   {3, 5, 0.988488096851},
        {3, 6, 0.995712186516},   {3, 7, 0.995712138153},
        {3, 8, 0.989029493146},   {3, 9, 0.988932200868},
        {3, 10, 0.008247318455},  {3, 11, 0.006717196872},
        {3, 12, 0.962898955781},  {3, 13, 0.961785108442},
        {3, 14, 0.037130964665},  {3, 15, 0.037582110744},
        {3, 16, 0.962898955662},  {3, 17, 0.961785108201},
        {3, 18, 0.037130964789},  {3, 19, 0.037582110882},
        {4, 0, 0.992730418328},   {4, 1, 0.992730863626},
        {4, 2, 0.988488096851},   {4, 3, 0.988938061303},
        {4, 4, 0.992735757778},   {4, 5, 0.987772924250},
        {4, 6, 0.992732165742},   {4, 7, 0.992732183777},
        {4, 8, 0.986013561840},   {4, 9, 0.986261146159},
        {4, 10, 0.004306012796},  {4, 11, 0.008563078162},
        {4, 12, 0.959954849889},  {4, 13, 0.959901281903},
        {4, 14, 0.036117295704},  {4, 15, 0.036174741718},
        {4, 16, 0.959954849600},  {4, 17, 0.959901281615},
        {4, 18, 0.036117295917},  {4, 19, 0.036174741930},
        {5, 0, 0.992730863626},   {5, 1, 0.992730418328},
        {5, 2, 0.988938061303},   {5, 3, 0.988488096851},
        {5, 4, 0.987772924250},   {5, 5, 0.992735757778},
        {5, 6, 0.992732183777},   {5, 7, 0.992732165742},
        {5, 8, 0.986261146159},   {5, 9, 0.986013561840},
        {5, 10, 0.008563078162},  {5, 11, 0.004306012796},
        {5, 12, 0.959901281903},  {5, 13, 0.959954849889},
        {5, 14, 0.036174741718},  {5, 15, 0.036117295704},
        {5, 16, 0.959901281615},  {5, 17, 0.959954849600},
        {5, 18, 0.036174741930},  {5, 19, 0.036117295917},
        {6, 0, 0.999991023723},   {6, 1, 0.999991042757},
        {6, 2, 0.995712138153},   {6, 3, 0.995712186516},
        {6, 4, 0.992732165742},   {6, 5, 0.992732183777},
        {6, 6, 0.999996382406},   {6, 7, 0.999994032616},
        {6, 8, 0.993182024657},   {6, 9, 0.993184262228},
        {6, 10, 0.009822977992},  {6, 11, 0.009822997419},
        {6, 12, 0.965702761794},  {6, 13, 0.965702768224},
        {6, 14, 0.038566220356},  {6, 15, 0.038566217607},
        {6, 16, 0.965702761518},  {6, 17, 0.965702767948},
        {6, 18, 0.038566220502},  {6, 19, 0.038566217752},
        {7, 0, 0.999991042757},   {7, 1, 0.999991023723},
        {7, 2, 0.995712186516},   {7, 3, 0.995712138153},
        {7, 4, 0.992732183777},   {7, 5, 0.992732165742},
        {7, 6, 0.999994032616},   {7, 7, 0.999996382406},
        {7, 8, 0.993184262228},   {7, 9, 0.993182024657},
        {7, 10, 0.009822997419},  {7, 11, 0.009822977992},
        {7, 12, 0.965702768224},  {7, 13, 0.965702761794},
        {7, 14, 0.038566217607},  {7, 15, 0.038566220356},
        {7, 16, 0.965702767948},  {7, 17, 0.965702761518},
        {7, 18, 0.038566217752},  {7, 19, 0.038566220502},
        {8, 0, 0.993180329514},   {8, 1, 0.993180356212},
        {8, 2, 0.988932200868},   {8, 3, 0.989029493146},
        {8, 4, 0.986013561840},   {8, 5, 0.986261146159},
        {8, 6, 0.993182024657},   {8, 7, 0.993184262228},
        {8, 8, 0.993185629977},   {8, 9, 0.992717426113},
        {8, 10, 0.009449354290},  {8, 11, 0.009567662747},
        {8, 12, 0.959035867939},  {8, 13, 0.959026757044},
        {8, 14, 0.035142286440},  {8, 15, 0.035152774605},
        {8, 16, 0.959035867685},  {8, 17, 0.959026756787},
        {8, 18, 0.035142286627},  {8, 19, 0.035152774795},
        {9, 0, 0.993180356212},   {9, 1, 0.993180329514},
        {9, 2, 0.989029493146},   {9, 3, 0.988932200868},
        {9, 4, 0.986261146159},   {9, 5, 0.986013561840},
        {9, 6, 0.993184262228},   {9, 7, 0.993182024657},
        {9, 8, 0.992717426113},   {9, 9, 0.993185629977},
        {9, 10, 0.009567662747},  {9, 11, 0.009449354290},
        {9, 12, 0.959026757044},  {9, 13, 0.959035867939},
        {9, 14, 0.035152774605},  {9, 15, 0.035142286440},
        {9, 16, 0.959026756787},  {9, 17, 0.959035867685},
        {9, 18, 0.035152774795},  {9, 19, 0.035142286627},
        {10, 0, 0.009821474521},  {10, 1, 0.009822425362},
        {10, 2, 0.006717196872},  {10, 3, 0.008247318455},
        {10, 4, 0.004306012796},  {10, 5, 0.008563078162},
        {10, 6, 0.009822977992},  {10, 7, 0.009822997419},
        {10, 8, 0.009449354290},  {10, 9, 0.009567662747},
        {10, 10, 0.009824647386}, {10, 11, 0.002594052873},
        {10, 12, 0.007538936621}, {10, 13, 0.007153519752},
        {10, 14, 0.001876497306}, {10, 15, 0.002923978542},
        {10, 16, 0.007538936620}, {10, 17, 0.007153519735},
        {10, 18, 0.001876497298}, {10, 19, 0.002923978655},
        {11, 0, 0.009822425362},  {11, 1, 0.009821474521},
        {11, 2, 0.008247318455},  {11, 3, 0.006717196872},
        {11, 4, 0.008563078162},  {11, 5, 0.004306012796},
        {11, 6, 0.009822997419},  {11, 7, 0.009822977992},
        {11, 8, 0.009567662747},  {11, 9, 0.009449354290},
        {11, 10, 0.002594052873}, {11, 11, 0.009824647386},
        {11, 12, 0.007153519752}, {11, 13, 0.007538936621},
        {11, 14, 0.002923978542}, {11, 15, 0.001876497306},
        {11, 16, 0.007153519735}, {11, 17, 0.007538936620},
        {11, 18, 0.002923978655}, {11, 19, 0.001876497298},
        {12, 0, 0.965701920093},  {12, 1, 0.965701606324},
        {12, 2, 0.961785108442},  {12, 3, 0.962898955781},
        {12, 4, 0.959954849889},  {12, 5, 0.959901281903},
        {12, 6, 0.965702761794},  {12, 7, 0.965702768224},
        {12, 8, 0.959035867939},  {12, 9, 0.959026757044},
        {12, 10, 0.007538936621}, {12, 11, 0.007153519752},
        {12, 12, 0.965706279830}, {12, 13, 0.951304710494},
        {12, 14, 0.006119061969}, {12, 15, 0.017428700416},
        {12, 16, 0.936040383373}, {12, 17, 0.941897827521},
        {12, 18, 0.032358788859}, {12, 19, 0.028927831353},
        {13, 0, 0.965701606324},  {13, 1, 0.965701920093},
        {13, 2, 0.962898955781},  {13, 3, 0.961785108442},
        {13, 4, 0.959901281903},  {13, 5, 0.959954849889},
        {13, 6, 0.965702768224},  {13, 7, 0.965702761794},
        {13, 8, 0.959026757044},  {13, 9, 0.959035867939},
        {13, 10, 0.007153519752}, {13, 11, 0.007538936621},
        {13, 12, 0.951304710494}, {13, 13, 0.965706279830},
        {13, 14, 0.017428700416}, {13, 15, 0.006119061969},
        {13, 16, 0.941897827521}, {13, 17, 0.936040383373},
        {13, 18, 0.028927831353}, {13, 19, 0.032358788859},
        {14, 0, 0.038565182138},  {14, 1, 0.038566359788},
        {14, 2, 0.037582110744},  {14, 3, 0.037130964665},
        {14, 4, 0.036117295704},  {14, 5, 0.036174741718},
        {14, 6, 0.038566220356},  {14, 7, 0.038566217607},
        {14, 8, 0.035142286440},  {14, 9, 0.035152774605},
        {14, 10, 0.001876497306}, {14, 11, 0.002923978542},
        {14, 12, 0.006119061969}, {14, 13, 0.017428700416},
        {14, 14, 0.038567318450}, {14, 15, 0.024165749114},
        {14, 16, 0.032358788675}, {14, 17, 0.028927831170},
        {14, 18, 0.005076467367}, {14, 19, 0.010933911525},
        {15, 0, 0.038566359788},  {15, 1, 0.038565182138},
        {15, 2, 0.037130964665},  {15, 3, 0.037582110744},
        {15, 4, 0.036174741718},  {15, 5, 0.036117295704},
        {15, 6, 0.038566217607},  {15, 7, 0.038566220356},
        {15, 8, 0.035152774605},  {15, 9, 0.035142286440},
        {15, 10, 0.002923978542}, {15, 11, 0.001876497306},
        {15, 12, 0.017428700416}, {15, 13, 0.006119061969},
        {15, 14, 0.024165749114}, {15, 15, 0.038567318450},
        {15, 16, 0.028927831170}, {15, 17, 0.032358788675},
        {15, 18, 0.010933911525}, {15, 19, 0.005076467367},
        {16, 0, 0.965701919817},  {16, 1, 0.965701606048},
        {16, 2, 0.961785108201},  {16, 3, 0.962898955662},
        {16, 4, 0.959954849600},  {16, 5, 0.959901281615},
        {16, 6, 0.965702761518},  {16, 7, 0.965702767948},
        {16, 8, 0.959035867685},  {16, 9, 0.959026756787},
        {16, 10, 0.007538936620}, {16, 11, 0.007153519735},
        {16, 12, 0.936040383373}, {16, 13, 0.941897827521},
        {16, 14, 0.032358788675}, {16, 15, 0.028927831170},
        {16, 16, 0.965706279554}, {16, 17, 0.951304710063},
        {16, 18, 0.006119061835}, {16, 19, 0.017428700329},
        {17, 0, 0.965701606048},  {17, 1, 0.965701919817},
        {17, 2, 0.962898955662},  {17, 3, 0.961785108201},
        {17, 4, 0.959901281615},  {17, 5, 0.959954849600},
        {17, 6, 0.965702767948},  {17, 7, 0.965702761518},
        {17, 8, 0.959026756787},  {17, 9, 0.959035867685},
        {17, 10, 0.007153519735}, {17, 11, 0.007538936620},
        {17, 12, 0.941897827521}, {17, 13, 0.936040383373},
        {17, 14, 0.028927831170}, {17, 15, 0.032358788675},
        {17, 16, 0.951304710063}, {17, 17, 0.965706279554},
        {17, 18, 0.017428700329}, {17, 19, 0.006119061835},
        {18, 0, 0.038565182284},  {18, 1, 0.038566359934},
        {18, 2, 0.037582110882},  {18, 3, 0.037130964789},
        {18, 4, 0.036117295917},  {18, 5, 0.036174741930},
        {18, 6, 0.038566220502},  {18, 7, 0.038566217752},
        {18, 8, 0.035142286627},  {18, 9, 0.035152774795},
        {18, 10, 0.001876497298}, {18, 11, 0.002923978655},
        {18, 12, 0.032358788859}, {18, 13, 0.028927831353},
        {18, 14, 0.005076467367}, {18, 15, 0.010933911525},
        {18, 16, 0.006119061835}, {18, 17, 0.017428700329},
        {18, 18, 0.038567318595}, {18, 19, 0.024165749104},
        {19, 0, 0.038566359934},  {19, 1, 0.038565182284},
        {19, 2, 0.037130964789},  {19, 3, 0.037582110882},
        {19, 4, 0.036174741930},  {19, 5, 0.036117295917},
        {19, 6, 0.038566217752},  {19, 7, 0.038566220502},
        {19, 8, 0.035152774795},  {19, 9, 0.035142286627},
        {19, 10, 0.002923978655}, {19, 11, 0.001876497298},
        {19, 12, 0.028927831353}, {19, 13, 0.032358788859},
        {19, 14, 0.010933911525}, {19, 15, 0.005076467367},
        {19, 16, 0.017428700329}, {19, 17, 0.006119061835},
        {19, 18, 0.024165749104}, {19, 19, 0.038567318595}};

    vector<tuple<int, int, double>> one_npc_mixed = {
        {0, 1, 0.000004198078},    {0, 3, -0.000001028445},
        {0, 5, -0.000000445298},   {0, 7, -0.000000019034},
        {0, 9, -0.000000026698},   {0, 11, -0.000000950844},
        {0, 13, 0.000000313771},   {0, 15, -0.000001177650},
        {0, 17, 0.000000313771},   {0, 19, -0.000001177650},
        {1, 0, 0.000004198078},    {1, 2, -0.000001028445},
        {1, 4, -0.000000445298},   {1, 6, -0.000000019034},
        {1, 8, -0.000000026698},   {1, 10, -0.000000950844},
        {1, 12, 0.000000313771},   {1, 14, -0.000001177650},
        {1, 16, 0.000000313771},   {1, 18, -0.000001177650},
        {2, 1, -0.000001028445},   {2, 3, 0.003403857749},
        {2, 5, -0.000449963972},   {2, 7, -0.000000048362},
        {2, 9, -0.000097292532},   {2, 11, -0.001530127540},
        {2, 13, -0.001113849537},  {2, 15, 0.000451151128},
        {2, 17, -0.001113849682},  {2, 19, 0.000451151194},
        {3, 0, -0.000001028445},   {3, 2, 0.003403857749},
        {3, 4, -0.000449963972},   {3, 6, -0.000000048362},
        {3, 8, -0.000097292532},   {3, 10, -0.001530127540},
        {3, 12, -0.001113849537},  {3, 14, 0.000451151128},
        {3, 16, -0.001113849682},  {3, 18, 0.000451151194},
        {4, 1, -0.000000445298},   {4, 3, -0.000449963972},
        {4, 5, 0.004962833527},    {4, 7, -0.000000018036},
        {4, 9, -0.000247584479},   {4, 11, -0.004257065632},
        {4, 13, 0.000053570316},   {4, 15, -0.000057448372},
        {4, 17, 0.000053570298},   {4, 19, -0.000057448353},
        {5, 0, -0.000000445298},   {5, 2, -0.000449963972},
        {5, 4, 0.004962833527},    {5, 6, -0.000000018036},
        {5, 8, -0.000247584479},   {5, 10, -0.004257065632},
        {5, 12, 0.000053570316},   {5, 14, -0.000057448372},
        {5, 16, 0.000053570298},   {5, 18, -0.000057448353},
        {6, 1, -0.000000019034},   {6, 3, -0.000000048362},
        {6, 5, -0.000000018036},   {6, 7, 0.000002349790},
        {6, 9, -0.000002237571},   {6, 11, -0.000000019428},
        {6, 13, -0.000000006429},  {6, 15, 0.000000002749},
        {6, 17, -0.000000006429},  {6, 19, 0.000000002749},
        {7, 0, -0.000000019034},   {7, 2, -0.000000048362},
        {7, 4, -0.000000018036},   {7, 6, 0.000002349790},
        {7, 8, -0.000002237571},   {7, 10, -0.000000019428},
        {7, 12, -0.000000006429},  {7, 14, 0.000000002749},
        {7, 16, -0.000000006429},  {7, 18, 0.000000002749},
        {8, 1, -0.000000026698},   {8, 3, -0.000097292532},
        {8, 5, -0.000247584479},   {8, 7, -0.000002237571},
        {8, 9, 0.000468203864},    {8, 11, -0.000118308025},
        {8, 13, 0.000009109105},   {8, 15, -0.000010486384},
        {8, 17, 0.000009109106},   {8, 19, -0.000010486386},
        {9, 0, -0.000000026698},   {9, 2, -0.000097292532},
        {9, 4, -0.000247584479},   {9, 6, -0.000002237571},
        {9, 8, 0.000468203864},    {9, 10, -0.000118308025},
        {9, 12, 0.000009109105},   {9, 14, -0.000010486384},
        {9, 16, 0.000009109106},   {9, 18, -0.000010486386},
        {10, 1, -0.000000950844},  {10, 3, -0.001530127540},
        {10, 5, -0.004257065632},  {10, 7, -0.000000019428},
        {10, 9, -0.000118308025},  {10, 11, 0.007230594513},
        {10, 13, 0.000385421383},  {10, 15, -0.001047482866},
        {10, 17, 0.000385421467},  {10, 19, -0.001047483029},
        {11, 0, -0.000000950844},  {11, 2, -0.001530127540},
        {11, 4, -0.004257065632},  {11, 6, -0.000000019428},
        {11, 8, -0.000118308025},  {11, 10, 0.007230594513},
        {11, 12, 0.000385421383},  {11, 14, -0.001047482866},
        {11, 16, 0.000385421467},  {11, 18, -0.001047483029},
        {12, 1, 0.000000313771},   {12, 3, -0.001113849537},
        {12, 5, 0.000053570316},   {12, 7, -0.000000006429},
        {12, 9, 0.000009109105},   {12, 11, 0.000385421383},
        {12, 13, 0.014401569336},  {12, 15, -0.011309669855},
        {12, 17, -0.005857484774}, {12, 19, 0.003431026684},
        {13, 0, 0.000000313771},   {13, 2, -0.001113849537},
        {13, 4, 0.000053570316},   {13, 6, -0.000000006429},
        {13, 8, 0.000009109105},   {13, 10, 0.000385421383},
        {13, 12, 0.014401569336},  {13, 14, -0.011309669855},
        {13, 16, -0.005857484774}, {13, 18, 0.003431026684},
        {14, 1, -0.000001177650},  {14, 3, 0.000451151128},
        {14, 5, -0.000057448372},  {14, 7, 0.000000002749},
        {14, 9, -0.000010486384},  {14, 11, -0.001047482866},
        {14, 13, -0.011309669855}, {14, 15, 0.014401569336},
        {14, 17, 0.003431026683},  {14, 19, -0.005857484770},
        {15, 0, -0.000001177650},  {15, 2, 0.000451151128},
        {15, 4, -0.000057448372},  {15, 6, 0.000000002749},
        {15, 8, -0.000010486384},  {15, 10, -0.001047482866},
        {15, 12, -0.011309669855}, {15, 14, 0.014401569336},
        {15, 16, 0.003431026683},  {15, 18, -0.005857484770},
        {16, 1, 0.000000313771},   {16, 3, -0.001113849682},
        {16, 5, 0.000053570298},   {16, 7, -0.000000006429},
        {16, 9, 0.000009109106},   {16, 11, 0.000385421467},
        {16, 13, -0.005857484774}, {16, 15, 0.003431026683},
        {16, 17, 0.014401569491},  {16, 19, -0.011309669930},
        {17, 0, 0.000000313771},   {17, 2, -0.001113849682},
        {17, 4, 0.000053570298},   {17, 6, -0.000000006429},
        {17, 8, 0.000009109106},   {17, 10, 0.000385421467},
        {17, 12, -0.005857484774}, {17, 14, 0.003431026683},
        {17, 16, 0.014401569491},  {17, 18, -0.011309669930},
        {18, 1, -0.000001177650},  {18, 3, 0.000451151194},
        {18, 5, -0.000057448353},  {18, 7, 0.000000002749},
        {18, 9, -0.000010486386},  {18, 11, -0.001047483029},
        {18, 13, 0.003431026684},  {18, 15, -0.005857484770},
        {18, 17, -0.011309669930}, {18, 19, 0.014401569491},
        {19, 0, -0.000001177650},  {19, 2, 0.000451151194},
        {19, 4, -0.000057448353},  {19, 6, 0.000000002749},
        {19, 8, -0.000010486386},  {19, 10, -0.001047483029},
        {19, 12, 0.003431026684},  {19, 14, -0.005857484770},
        {19, 16, -0.011309669930}, {19, 18, 0.014401569491}};

    vector<tuple<int, int, int, int, double>> two_pdm[6];
    load_twopdm("data/N2.STO3G.2PDM", two_pdm);

    Timer t;
    t.get_time();
    // MPO construction
    cout << "MPO start" << endl;
    shared_ptr<MPO<SZ>> mpo =
        make_shared<MPOQC<SZ>>(hamil, QCTypes::Conventional);
    cout << "MPO end .. T = " << t.get_time() << endl;

    // MPO simplification
    cout << "MPO simplification start" << endl;
    mpo = make_shared<SimplifiedMPO<SZ>>(mpo, make_shared<RuleQC<SZ>>(), true);
    cout << "MPO simplification end .. T = " << t.get_time() << endl;

    // MPO parallelization
    cout << "MPO parallelization start" << endl;
    mpo = make_shared<ParallelMPO<SZ>>(mpo, para_rule);
    cout << "MPO parallelization end .. T = " << t.get_time() << endl;

    // 1PDM MPO construction
    cout << "1PDM MPO start" << endl;
    shared_ptr<MPO<SZ>> pmpo = make_shared<PDM1MPOQC<SZ>>(hamil);
    cout << "1PDM MPO end .. T = " << t.get_time() << endl;

    // 1PDM MPO simplification
    cout << "1PDM MPO simplification start" << endl;
    pmpo =
        make_shared<SimplifiedMPO<SZ>>(pmpo, make_shared<RuleQC<SZ>>(), true);
    cout << "1PDM MPO simplification end .. T = " << t.get_time() << endl;

    // 1PDM MPO parallelization
    cout << "1PDM MPO parallelization start" << endl;
    pmpo = make_shared<ParallelMPO<SZ>>(pmpo, pdm1_para_rule);
    cout << "1PDM MPO parallelization end .. T = " << t.get_time() << endl;
    // cout << pmpo->get_blocking_formulas() << endl;
    // abort();

    // 2PDM MPO construction
    cout << "2PDM MPO start" << endl;
    shared_ptr<MPO<SZ>> p2mpo = make_shared<PDM2MPOQC<SZ>>(hamil);
    cout << "2PDM MPO end .. T = " << t.get_time() << endl;

    // 2PDM MPO simplification
    cout << "2PDM MPO simplification start" << endl;
    p2mpo =
        make_shared<SimplifiedMPO<SZ>>(p2mpo, make_shared<RuleQC<SZ>>(), true);
    cout << "2PDM MPO simplification end .. T = " << t.get_time() << endl;

    // 2PDM MPO parallelization
    cout << "2PDM MPO parallelization start" << endl;
    p2mpo = make_shared<ParallelMPO<SZ>>(p2mpo, pdm2_para_rule);
    cout << "2PDM MPO parallelization end .. T = " << t.get_time() << endl;

    // 1NPC MPO construction
    cout << "1NPC MPO start" << endl;
    shared_ptr<MPO<SZ>> nmpo = make_shared<NPC1MPOQC<SZ>>(hamil);
    cout << "1NPC MPO end .. T = " << t.get_time() << endl;

    // 1NPC MPO simplification
    cout << "1NPC MPO simplification start" << endl;
    nmpo = make_shared<SimplifiedMPO<SZ>>(nmpo, make_shared<Rule<SZ>>(), true);
    cout << "1NPC MPO simplification end .. T = " << t.get_time() << endl;

    // 1NPC MPO parallelization
    cout << "1NPC MPO parallelization start" << endl;
    nmpo = make_shared<ParallelMPO<SZ>>(nmpo, pdm1_para_rule);
    cout << "1NPC MPO parallelization end .. T = " << t.get_time() << endl;

    ubond_t bond_dim = 200;

    for (int dot = 1; dot <= 2; dot++) {

        // MPSInfo
        shared_ptr<MPSInfo<SZ>> mps_info =
            make_shared<MPSInfo<SZ>>(norb, vacuum, target, hamil->basis);
        mps_info->set_bond_dimension(bond_dim);

        // MPS
        Random::rand_seed(0);
        shared_ptr<MPS<SZ>> mps = make_shared<MPS<SZ>>(norb, 0, dot);
        mps->initialize(mps_info);
        mps->random_canonicalize();

        // MPS/MPSInfo save mutable
        mps->save_mutable();
        mps->deallocate();
        mps_info->save_mutable();
        mps_info->deallocate_mutable();

        // ME
        shared_ptr<MovingEnvironment<SZ>> me =
            make_shared<MovingEnvironment<SZ>>(mpo, mps, mps, "DMRG");
        t.get_time();
        cout << "INIT start" << endl;
        me->init_environments(false);
        cout << "INIT end .. T = " << t.get_time() << endl;

        // DMRG
        vector<ubond_t> bdims = {bond_dim};
        vector<double> noises = {1E-8, 0};
        shared_ptr<DMRG<SZ>> dmrg = make_shared<DMRG<SZ>>(me, bdims, noises);
        dmrg->iprint = 2;
        dmrg->noise_type = NoiseTypes::ReducedPerturbativeCollected;
        dmrg->solve(10, true, 1E-12);

        // 1PDM ME
        shared_ptr<MovingEnvironment<SZ>> pme =
            make_shared<MovingEnvironment<SZ>>(pmpo, mps, mps, "1PDM");
        t.get_time();
        cout << "1PDM INIT start" << endl;
        pme->init_environments(false);
        cout << "1PDM INIT end .. T = " << t.get_time() << endl;

        // 1PDM
        shared_ptr<Expect<SZ>> expect =
            make_shared<Expect<SZ>>(pme, bond_dim, bond_dim);
        expect->solve(true, mps->center == 0);

        MatrixRef dm = expect->get_1pdm_spatial();
        int k = 0;
        for (int i = 0; i < dm.m; i++)
            for (int j = 0; j < dm.n; j++)
                if (abs(dm(i, j)) > TINY) {
                    cout << "== SZ 1PDM SPAT / " << dot << "-site ==" << setw(5)
                         << i << setw(5) << j << fixed << setw(22) << fixed
                         << setprecision(12) << dm(i, j)
                         << " error = " << scientific << setprecision(3)
                         << setw(10) << abs(dm(i, j) - get<2>(one_pdm[k]))
                         << endl;

                    EXPECT_EQ(i, get<0>(one_pdm[k]));
                    EXPECT_EQ(j, get<1>(one_pdm[k]));
                    EXPECT_LT(abs(dm(i, j) - get<2>(one_pdm[k])), 1E-6);

                    k++;
                }

        EXPECT_EQ(k, (int)one_pdm.size());

        dm.deallocate();

        dm = expect->get_1pdm();
        int kk[2] = {0, 0};
        for (int i = 0; i < dm.m; i++)
            for (int j = 0; j < dm.n; j++)
                if (abs(dm(i, j)) > TINY) {
                    EXPECT_EQ(i % 2, j % 2);
                    int ii = i / 2, jj = j / 2, p = i % 2;

                    cout << "== SZ 1PDM / " << dot << "-site ==" << setw(6)
                         << (p == 0 ? "alpha" : "beta") << setw(5) << ii
                         << setw(5) << jj << fixed << setw(22) << fixed
                         << setprecision(12) << dm(i, j)
                         << " error = " << scientific << setprecision(3)
                         << setw(10)
                         << abs(dm(i, j) - get<2>(one_pdm[kk[p]]) / 2) << endl;

                    EXPECT_EQ(ii, get<0>(one_pdm[kk[p]]));
                    EXPECT_EQ(jj, get<1>(one_pdm[kk[p]]));
                    EXPECT_LT(abs(dm(i, j) - get<2>(one_pdm[kk[p]]) / 2), 1E-6);

                    kk[p]++;
                }

        EXPECT_EQ(kk[0], (int)one_pdm.size());
        EXPECT_EQ(kk[1], (int)one_pdm.size());

        dm.deallocate();

        // 2PDM ME
        shared_ptr<MovingEnvironment<SZ>> p2me =
            make_shared<MovingEnvironment<SZ>>(p2mpo, mps, mps, "2PDM");
        t.get_time();
        cout << "2PDM INIT start" << endl;
        p2me->init_environments(false);
        cout << "2PDM INIT end .. T = " << t.get_time() << endl;

        // 2PDM
        expect = make_shared<Expect<SZ>>(p2me, bond_dim, bond_dim);
        expect->solve(true, mps->center == 0);

        int m[6] = {0, 0, 0, 0, 0, 0};
        double max_error = 0.0;
        shared_ptr<Tensor> dm2 = expect->get_2pdm();
        for (int i = 0; i < dm2->shape[0]; i++)
            for (int j = 0; j < dm2->shape[1]; j++)
                for (int k = 0; k < dm2->shape[2]; k++)
                    for (int l = 0; l < dm2->shape[3]; l++)
                        if (abs((*dm2)({i, j, k, l})) > TINY) {

                            int p = -1;
                            double f = 1.0;

                            int ii = i / 2, jj = j / 2, kk = k / 2, ll = l / 2;

                            if (i % 2 == 0 && j % 2 == 0 && k % 2 == 0 &&
                                l % 2 == 0)
                                p = 0, f = 1.0;
                            else if (i % 2 == 1 && j % 2 == 1 && k % 2 == 1 &&
                                     l % 2 == 1)
                                p = 1, f = 1.0;
                            else if (i % 2 == 0 && j % 2 == 1 && k % 2 == 1 &&
                                     l % 2 == 0)
                                p = 2, f = 1.0;
                            else if (i % 2 == 0 && j % 2 == 1 && k % 2 == 0 &&
                                     l % 2 == 1)
                                p = 3, f = -1.0, swap(kk, ll);
                            else if (i % 2 == 1 && j % 2 == 0 && k % 2 == 1 &&
                                     l % 2 == 0)
                                p = 4, f = -1.0, swap(ii, jj);
                            else if (i % 2 == 1 && j % 2 == 0 && k % 2 == 0 &&
                                     l % 2 == 1)
                                p = 5, f = 1.0, swap(ii, jj), swap(kk, ll);

                            EXPECT_NE(p, -1);

                            EXPECT_EQ(ii, get<0>(two_pdm[p][m[p]]));
                            EXPECT_EQ(jj, get<1>(two_pdm[p][m[p]]));
                            EXPECT_EQ(kk, get<2>(two_pdm[p][m[p]]));
                            EXPECT_EQ(ll, get<3>(two_pdm[p][m[p]]));
                            EXPECT_LT(abs((*dm2)({i, j, k, l}) -
                                          f * get<4>(two_pdm[p][m[p]])),
                                      1E-6);

                            max_error = max(max_error,
                                            abs((*dm2)({i, j, k, l}) -
                                                f * get<4>(two_pdm[p][m[p]])));

                            m[p]++;
                        }

        for (int p = 0; p < 6; p++)
            EXPECT_EQ(m[p], (int)two_pdm[p].size());

        cout << "== SZ 2PDM / " << dot << "-site =="
             << " max error = " << scientific << setprecision(3) << setw(10)
             << max_error << endl;

        m[0] = m[1] = m[2] = 0;
        max_error = 0.0;
        dm2 = expect->get_2pdm_spatial();
        for (int i = 0; i < dm2->shape[0]; i++)
            for (int j = 0; j < dm2->shape[1]; j++)
                for (int k = 0; k < dm2->shape[2]; k++)
                    for (int l = 0; l < dm2->shape[3]; l++)
                        if (abs((*dm2)({i, j, k, l})) > TINY) {

                            double v = 0;
                            if (m[0] < two_pdm[0].size() &&
                                two_pdm[0][m[0]] ==
                                    make_tuple(i, j, k, l,
                                               get<4>(two_pdm[0][m[0]])))
                                v += get<4>(two_pdm[0][m[0]]), m[0]++;
                            if (m[1] < two_pdm[1].size() &&
                                two_pdm[1][m[1]] ==
                                    make_tuple(i, j, k, l,
                                               get<4>(two_pdm[1][m[1]])))
                                v += get<4>(two_pdm[1][m[1]]), m[1]++;
                            if (m[2] < two_pdm[2].size() &&
                                two_pdm[2][m[2]] ==
                                    make_tuple(i, j, k, l,
                                               get<4>(two_pdm[2][m[2]])))
                                v += get<4>(two_pdm[2][m[2]]) * 2, m[2]++;

                            EXPECT_LT(abs((*dm2)({i, j, k, l}) - v), 1E-6);

                            max_error =
                                max(max_error, abs((*dm2)({i, j, k, l}) - v));
                        }

        for (int p = 0; p < 3; p++)
            EXPECT_EQ(m[p], (int)two_pdm[p].size());

        cout << "== SZ 2PDM SPAT / " << dot << "-site =="
             << " max error = " << scientific << setprecision(3) << setw(10)
             << max_error << endl;

        // 1NPC ME
        shared_ptr<MovingEnvironment<SZ>> nme =
            make_shared<MovingEnvironment<SZ>>(nmpo, mps, mps, "1NPC");
        t.get_time();
        cout << "1NPC INIT start" << endl;
        nme->init_environments(false);
        cout << "1NPC INIT end .. T = " << t.get_time() << endl;

        // 1NPC
        expect = make_shared<Expect<SZ>>(nme, bond_dim, bond_dim);
        expect->solve(true, mps->center == 0);

        MatrixRef dmx = expect->get_1npc(0);

        int kx = 0;
        for (int i = 0; i < dmx.m; i++)
            for (int j = 0; j < dmx.n; j++)
                if (abs(dmx(i, j)) > TINY) {

                    cout << "== SZ 1NPC  PURE / " << dot
                         << "-site ==" << setw(5) << i << setw(5) << j << fixed
                         << setw(22) << fixed << setprecision(12) << dmx(i, j)
                         << " error = " << scientific << setprecision(3)
                         << setw(10)
                         << abs(dmx(i, j) - get<2>(one_npc_pure[kx])) << endl;

                    EXPECT_EQ(i, get<0>(one_npc_pure[kx]));
                    EXPECT_EQ(j, get<1>(one_npc_pure[kx]));
                    EXPECT_LT(abs(dmx(i, j) - get<2>(one_npc_pure[kx])), 1E-6);

                    kx++;
                }

        EXPECT_EQ(kx, (int)one_npc_pure.size());

        dmx.deallocate();

        MatrixRef dmy = expect->get_1npc(1);

        int ky = 0;
        for (int i = 0; i < dmy.m; i++)
            for (int j = 0; j < dmy.n; j++)
                if (abs(dmy(i, j)) > TINY) {

                    cout << "== SZ 1NPC MIXED / " << dot
                         << "-site ==" << setw(5) << i << setw(5) << j << fixed
                         << setw(22) << fixed << setprecision(12) << dmy(i, j)
                         << " error = " << scientific << setprecision(3)
                         << setw(10)
                         << abs(dmy(i, j) - get<2>(one_npc_mixed[ky])) << endl;

                    EXPECT_EQ(i, get<0>(one_npc_mixed[ky]));
                    EXPECT_EQ(j, get<1>(one_npc_mixed[ky]));
                    EXPECT_LT(abs(dmy(i, j) - get<2>(one_npc_mixed[ky])), 1E-6);

                    ky++;
                }

        EXPECT_EQ(ky, (int)one_npc_mixed.size());

        dmy.deallocate();

        // deallocate persistent stack memory
        mps_info->deallocate();
    }

    nmpo->deallocate();
    p2mpo->deallocate();
    pmpo->deallocate();
    mpo->deallocate();
    hamil->deallocate();
    fcidump->deallocate();
}
