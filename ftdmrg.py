
#
# FT-DMRG using pyscf and block2
#
# Author: Huanchen Zhai, May 14, 2020
#
# Revised:     added sz, May 18, 2020
#

import sys
sys.path[:0] = ["./build"]

import numpy as np
import time
from block2 import init_memory, release_memory, set_mkl_num_threads
from block2 import VectorUInt8, VectorUInt16, VectorDouble, PointGroup
from block2 import Random, FCIDUMP, QCTypes, SeqTypes, TETypes
from block2 import SU2, SZ

# Set spin-adapted or non-spin-adapted here
SpinLabel = SU2
# SpinLabel = SZ

if SpinLabel == SU2:
    from block2.su2 import HamiltonianQC, AncillaMPSInfo, MPS
    from block2.su2 import AncillaMPO, PDM1MPOQC, SimplifiedMPO, Rule, RuleQC, MPOQC
    from block2.su2 import MovingEnvironment, ImaginaryTE, Expect
else:
    from block2.sz import HamiltonianQC, AncillaMPSInfo, MPS
    from block2.sz import AncillaMPO, PDM1MPOQC, SimplifiedMPO, Rule, RuleQC, MPOQC
    from block2.sz import MovingEnvironment, ImaginaryTE, Expect


class FTDMRGError(Exception):
    pass


class FTDMRG:
    """
    Finite-temperature DMRG for molecules.
    """

    def __init__(self, su2=True, scratch='./nodex', memory=1 * 1E9, omp_threads=2, verbose=2):

        assert su2
        Random.rand_seed(0)
        init_memory(isize=int(memory * 0.1),
                    dsize=int(memory * 0.9), save_dir=scratch)
        set_mkl_num_threads(omp_threads)
        self.fcidump = None
        self.hamil = None
        self.verbose = verbose

    def init_hamiltonian_fcidump(self, pg, filename):
        assert self.fcidump is None
        self.fcidump = FCIDUMP()
        self.fcidump.read(filename)
        self.orb_sym = VectorUInt8(
            map(PointGroup.swap_d2h, self.fcidump.orb_sym))
        n_elec = self.fcidump.n_sites * 2

        vaccum = SpinLabel(0)
        target = SpinLabel(n_elec, self.fcidump.twos,
                           PointGroup.swap_d2h(self.fcidump.isym))
        self.n_physical_sites = self.fcidump.n_sites
        self.n_sites = self.fcidump.n_sites * 2

        self.hamil = HamiltonianQC(
            vaccum, target, self.n_physical_sites, self.orb_sym, self.fcidump)
        self.hamil.opf.seq.mode = SeqTypes.Simple
        assert pg in ["d2h", "c1"]

    def init_hamiltonian(self, pg, n_sites, twos, isym, orb_sym, e_core, h1e, g2e, tol=1E-13):
        assert self.fcidump is None
        self.fcidump = FCIDUMP()
        n_elec = n_sites * 2
        if not isinstance(h1e, tuple):
            mh1e = np.zeros((n_sites * (n_sites + 1) // 2))
            k = 0
            for i in range(0, n_sites):
                for j in range(0, i + 1):
                    assert abs(h1e[i, j] - h1e[j, i]) < tol
                    mh1e[k] = h1e[i, j]
                    k += 1
            mg2e = g2e.flatten().copy()
            mh1e[np.abs(mh1e) < tol] = 0.0
            mg2e[np.abs(mg2e) < tol] = 0.0
            self.fcidump.initialize_su2(
                n_sites, n_elec, twos, isym, e_core, mh1e, mg2e)
        else:
            assert SpinLabel == SZ
            assert isinstance(h1e, tuple) and len(h1e) == 2
            assert isinstance(g2e, tuple) and len(g2e) == 3
            mh1e_a = np.zeros((n_sites * (n_sites + 1) // 2))
            mh1e_b = np.zeros((n_sites * (n_sites + 1) // 2))
            mh1e = (mh1e_a, mh1e_b)
            for xmh1e, xh1e in zip(mh1e, h1e):
                k = 0
                for i in range(0, n_sites):
                    for j in range(0, i + 1):
                        assert abs(xh1e[i, j] - xh1e[j, i]) < tol
                        xmh1e[k] = xh1e[i, j]
                        k += 1
                xmh1e[np.abs(xmh1e) < tol] = 0.0
            mg2e = tuple(xg2e.flatten().copy() for xg2e in g2e)
            for xmg2e in mg2e:
                xmg2e[np.abs(xmg2e) < tol] = 0.0
            self.fcidump.initialize_sz(
                n_sites, n_elec, twos, isym, e_core, mh1e, mg2e)
        self.orb_sym = VectorUInt8(map(PointGroup.swap_d2h, orb_sym))

        vaccum = SpinLabel(0)
        target = SpinLabel(n_elec, twos, PointGroup.swap_d2h(isym))
        self.n_physical_sites = n_sites
        self.n_sites = n_sites * 2

        self.hamil = HamiltonianQC(
            vaccum, target, self.n_physical_sites, self.orb_sym, self.fcidump)
        self.hamil.opf.seq.mode = SeqTypes.Simple

        assert pg in ["d2h", "c1"]

    def generate_initial_mps(self, bond_dim):
        if self.verbose >= 2:
            print('>>> START generate initial mps <<<')
        t = time.perf_counter()
        assert self.hamil is not None

        # Ancilla MPSInfo (thermal)
        mps_info_thermal = AncillaMPSInfo(self.n_physical_sites, self.hamil.vaccum,
                                          self.hamil.target, self.hamil.basis,
                                          self.hamil.orb_sym, self.hamil.n_syms)
        mps_info_thermal.set_thermal_limit()
        mps_info_thermal.tag = "INIT"
        mps_info_thermal.save_mutable()
        mps_info_thermal.deallocate_mutable()

        if self.verbose >= 2:
            print("left dims = ", [
                p.n_states_total for p in mps_info_thermal.left_dims])
            print("right dims = ", [
                p.n_states_total for p in mps_info_thermal.right_dims])

        # Ancilla MPS (thermal)
        mps_thermal = MPS(self.n_sites, self.n_sites - 2, 2)
        mps_info_thermal.load_mutable()
        mps_thermal.initialize(mps_info_thermal)
        mps_thermal.fill_thermal_limit()
        mps_thermal.canonicalize()

        mps_thermal.save_mutable()
        mps_thermal.deallocate()
        mps_info_thermal.deallocate_mutable()

        mps_thermal.save_data()
        mps_info_thermal.deallocate()

        if self.verbose >= 2:
            print('>>> COMPLETE generate initial mps | Time = %.2f <<<' %
                  (time.perf_counter() - t))

    def imaginary_time_evolution(self, n_steps, beta_step, mu, bond_dims,
                                 method=TETypes.RK4, n_sub_sweeps=4, cont=False):
        if self.verbose >= 2:
            print('>>> START imaginary time evolution <<<')
        t = time.perf_counter()

        self.hamil.mu = mu

        # Ancilla MPSInfo (initial)
        mps_info = AncillaMPSInfo(self.n_physical_sites, self.hamil.vaccum,
                                  self.hamil.target, self.hamil.basis,
                                  self.hamil.orb_sym, self.hamil.n_syms)
        mps_info.tag = "INIT" if not cont else "FINAL"
        mps_info.load_mutable()

        # Ancilla MPS (initial)
        mps = MPS(mps_info)
        mps.load_data()
        mps.load_mutable()

        # MPS/MPSInfo save mutable
        if not cont:
            mps_info.tag = "FINAL"
            mps_info.save_mutable()
            mps.save_mutable()
        mps.deallocate()
        mps_info.deallocate_mutable()

        # MPO
        tx = time.perf_counter()
        mpo = MPOQC(self.hamil, QCTypes.Conventional)
        mpo = SimplifiedMPO(AncillaMPO(mpo), RuleQC())
        if self.verbose >= 2:
            print('MPO time = ', time.perf_counter() - tx)

        # TE
        me = MovingEnvironment(mpo, mps, mps, "TE")
        tx = time.perf_counter()
        me.init_environments(self.verbose >= 3)
        if self.verbose >= 2:
            print('TE INIT time = ', time.perf_counter() - tx)
        te = ImaginaryTE(me, VectorUInt16(bond_dims), method, n_sub_sweeps)
        te.iprint = self.verbose
        te.solve(n_steps, beta_step, mps.center == 0)

        self.bond_dim = bond_dims[-1]
        mps.save_data()
        mpo.deallocate()
        mps_info.deallocate()

        if self.verbose >= 2:
            print('>>> COMPLETE imaginary time evolution | Time = %.2f <<<' %
                  (time.perf_counter() - t))

    def get_one_pdm(self, ridx=None):
        if self.verbose >= 2:
            print('>>> START one-pdm <<<')
        t = time.perf_counter()

        self.hamil.mu = 0.0

        # Ancilla MPSInfo (final)
        mps_info = AncillaMPSInfo(self.n_physical_sites, self.hamil.vaccum,
                                  self.hamil.target, self.hamil.basis,
                                  self.hamil.orb_sym, self.hamil.n_syms)
        mps_info.tag = "FINAL"

        # Ancilla MPS (final)
        mps = MPS(mps_info)
        mps.load_data()

        # 1PDM MPO
        pmpo = PDM1MPOQC(self.hamil)
        pmpo = AncillaMPO(pmpo, True)
        pmpo = SimplifiedMPO(pmpo, Rule())

        # 1PDM
        pme = MovingEnvironment(pmpo, mps, mps, "1PDM")
        pme.init_environments(self.verbose >= 3)
        expect = Expect(pme, self.bond_dim, self.bond_dim)
        expect.iprint = self.verbose
        expect.solve(True, mps.center == 0)
        if SpinLabel == SU2:
            dmr = expect.get_1pdm_spatial(self.n_physical_sites)
            dm = np.array(dmr).copy()
        else:
            dmr = expect.get_1pdm(self.n_physical_sites)
            dm = np.array(dmr).copy()
            dm = dm.reshape((self.n_physical_sites, 2,
                             self.n_physical_sites, 2))
            dm = np.transpose(dm, (0, 2, 1, 3))

        if ridx is not None:
            dm[:, :] = dm[ridx, :][:, ridx]

        dmr.deallocate()
        pmpo.deallocate()
        mps_info.deallocate()

        if self.verbose >= 2:
            print('>>> COMPLETE one-pdm | Time = %.2f <<<' %
                  (time.perf_counter() - t))

        if SpinLabel == SU2:
            return np.concatenate([dm[None, :, :], dm[None, :, :]], axis=0) / 2
        else:
            return np.concatenate([dm[None, :, :, 0, 0], dm[None, :, :, 1, 1]], axis=0)

    def __del__(self):
        if self.hamil is not None:
            self.hamil.deallocate()
        if self.fcidump is not None:
            self.fcidump.deallocate()
        release_memory()


if __name__ == "__main__":

    # parameters
    bond_dim = 1000
    beta = 1.0
    beta_step = 0.1
    mu = -1.0
    bond_dims = [bond_dim]
    n_threads = 8
    hf_type = "RHF"
    pg_reorder = True
    scratch = '/central/scratch/hczhai/hchain'
    scratch = './nodex'
    scratch = '/scratch/local/hczhai/hchain'

    import os
    if not os.path.isdir(scratch):
        os.mkdir(scratch)
    os.environ['TMPDIR'] = scratch

    from pyscf import gto, scf, symm, ao2mo

    # H chain
    N = 8
    BOHR = 0.52917721092  # Angstroms
    R = 1.8 * BOHR
    mol = gto.M(atom=[['H', (i * R, 0, 0)] for i in range(N)],
                basis='sto6g', verbose=0, symmetry='d2h')
    pg = mol.symmetry.lower()

    # Reorder
    if pg == 'd2h':
        fcidump_sym = ["Ag", "B3u", "B2u", "B1g", "B1u", "B2g", "B3g", "Au"]
        optimal_reorder = ["Ag", "B1u", "B3u",
                           "B2g", "B2u", "B3g", "B1g", "Au"]
    elif pg == 'c1':
        fcidump_sym = ["A"]
        optimal_reorder = ["A"]
    else:
        raise FTDMRGError("Point group %d not supported yet!" % pg)
    
    if hf_type == "RHF":
        # SCF
        m = scf.RHF(mol)
        m.kernel()
        mo_coeff = m.mo_coeff
        n_ao = mo_coeff.shape[0]
        n_mo = mo_coeff.shape[1]

        orb_sym_str = symm.label_orb_symm(
            mol, mol.irrep_name, mol.symm_orb, mo_coeff)
        orb_sym = np.array([fcidump_sym.index(i) + 1 for i in orb_sym_str])

        # Sort the orbitals by symmetry for more efficient DMRG
        if pg_reorder:
            idx = np.argsort([optimal_reorder.index(i) for i in orb_sym_str])
            orb_sym = orb_sym[idx]
            mo_coeff = mo_coeff[:, idx]
            ridx = np.argsort(idx)
        else:
            ridx = np.array(list(range(n_mo), dtype=int))

        h1e = mo_coeff.T @ m.get_hcore() @ mo_coeff
        g2e = ao2mo.restore(8, ao2mo.kernel(mol, mo_coeff), n_mo)
        ecore = mol.energy_nuc()
        ecore = 0.0

    elif hf_type == "UHF":
        assert SpinLabel == SZ
        # SCF
        m = scf.UHF(mol)
        m.kernel()
        mo_coeff_a, mo_coeff_b = m.mo_coeff[0], m.mo_coeff[1]
        n_ao = mo_coeff_a.shape[0]
        n_mo = mo_coeff_b.shape[1]

        orb_sym_str_a = symm.label_orb_symm(mol, mol.irrep_name, mol.symm_orb, mo_coeff_a)
        orb_sym_str_b = symm.label_orb_symm(mol, mol.irrep_name, mol.symm_orb, mo_coeff_b)
        orb_sym_a = np.array([fcidump_sym.index(i) + 1 for i in orb_sym_str_a])
        orb_sym_b = np.array([fcidump_sym.index(i) + 1 for i in orb_sym_str_b])

        # Sort the orbitals by symmetry for more efficient DMRG
        if pg_reorder:
            idx_a = np.argsort([optimal_reorder.index(i) for i in orb_sym_str_a])
            orb_sym_a = orb_sym_a[idx_a]
            mo_coeff_a = mo_coeff_a[:, idx_a]
            idx_b = np.argsort([optimal_reorder.index(i) for i in orb_sym_str_b])
            orb_sym_b = orb_sym_b[idx_b]
            mo_coeff_b = mo_coeff_b[:, idx_b]
            assert np.allclose(idx_a, idx_b)
            assert np.allclose(orb_sym_a, orb_sym_b)
            orb_sym = orb_sym_a
            ridx = np.argsort(idx_a)
        else:
            ridx = np.array(list(range(n_mo), dtype=int))

        h1ea = mo_coeff_a.T @ m.get_hcore() @ mo_coeff_a
        h1eb = mo_coeff_b.T @ m.get_hcore() @ mo_coeff_b
        g2eaa = ao2mo.restore(8, ao2mo.kernel(mol, mo_coeff_a), n_mo)
        g2ebb = ao2mo.restore(8, ao2mo.kernel(mol, mo_coeff_b), n_mo)
        g2eab = ao2mo.kernel(mol, [mo_coeff_a, mo_coeff_a, mo_coeff_b, mo_coeff_b])
        h1e = (h1ea, h1eb)
        g2e = (g2eaa, g2ebb, g2eab)
        ecore = mol.energy_nuc()
        ecore = 0.0

    ft = FTDMRG(scratch=scratch, memory=20E9, verbose=2, omp_threads=n_threads)
    ft.init_hamiltonian(pg, n_sites=n_mo, twos=0, isym=1,
                        orb_sym=orb_sym, e_core=ecore, h1e=h1e, g2e=g2e)
    ft.generate_initial_mps(bond_dim)
    n_steps = int(round(beta / beta_step) + 0.1)
    # use 4 sweeps for the first beta step
    ft.imaginary_time_evolution(1, beta_step, mu, bond_dims, TETypes.RK4, n_sub_sweeps=6)
    # after the first beta step, use 2 sweeps (or 1 sweep) for each beta step
    # ft.imaginary_time_evolution(n_steps - 1, beta_step, mu, bond_dims, TETypes.RK4, n_sub_sweeps=2, cont=True)
    print(ft.get_one_pdm(ridx))