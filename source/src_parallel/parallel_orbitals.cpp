#include "parallel_orbitals.h"
#include "../module_base/memory.h"
#include "module_orbital/ORB_control.h"
#ifdef __MPI
extern "C"
{
    #include "../module_base/blacs_connector.h"
	#include "../module_base/scalapack_connector.h"
}
#endif

Parallel_Orbitals::Parallel_Orbitals()
{
    loc_sizes = new int[1];
    trace_loc_row = new int[1];
    trace_loc_col = new int[1];

    testpb = 0;//mohan add 2011-03-16
	alloc_Z_LOC = false; //xiaohui add 2014-12-22
    // default value of nb is 1,
	// but can change to larger value from input.
    nb = 1;
	MatrixInfo.row_set = new int[1];
	MatrixInfo.col_set = new int[1];
}

Parallel_Orbitals::~Parallel_Orbitals()
{
    delete[] trace_loc_row;
    delete[] trace_loc_col;
    delete loc_sizes;
    
    if (alloc_Z_LOC)//xiaohui add 2014-12-22
	{
		for(int is=0; is<GlobalV::NSPIN; is++)
		{
			delete[] Z_LOC[is];
		}
		delete[] Z_LOC;
	}
    delete[] MatrixInfo.row_set;
	delete[] MatrixInfo.col_set;

}

bool Parallel_Orbitals::in_this_processor(const int &iw1_all, const int &iw2_all) const
{
    if (trace_loc_row[iw1_all] == -1) return false;
    else if (trace_loc_col[iw2_all] == -1) return false;
    return true;
}

void ORB_control::set_trace(void)
{
    ModuleBase::TITLE("ORB_control","set_trace");
    assert(GlobalV::NLOCAL > 0);
    
    Parallel_Orbitals* pv = &this->ParaV;

    delete[] pv->trace_loc_row;
    delete[] pv->trace_loc_col;

    ModuleBase::GlobalFunc::OUT(GlobalV::ofs_running,"trace_loc_row dimension",GlobalV::NLOCAL);
    ModuleBase::GlobalFunc::OUT(GlobalV::ofs_running,"trace_loc_col dimension",GlobalV::NLOCAL);

    pv->trace_loc_row = new int[GlobalV::NLOCAL];
    pv->trace_loc_col = new int[GlobalV::NLOCAL];
    // mohan update 2011-04-07
    for(int i=0; i<GlobalV::NLOCAL; i++)
    {
        pv->trace_loc_row[i] = -1;
        pv->trace_loc_col[i] = -1;
    }

    ModuleBase::Memory::record("ORB_control","trace_loc_row",GlobalV::NLOCAL,"int");
    ModuleBase::Memory::record("ORB_control","trace_loc_col",GlobalV::NLOCAL,"int");

    if(GlobalV::KS_SOLVER=="lapack"
    || GlobalV::KS_SOLVER=="cg"
    || GlobalV::KS_SOLVER=="dav") //xiaohui add 2013-09-02
	{
		std::cout << " common settings for trace_loc_row and trace_loc_col " << std::endl;
		for (int i=0; i<GlobalV::NLOCAL; i++)
		{
			pv->trace_loc_row[i] = i;
			pv->trace_loc_col[i] = i;
		}
		pv->nrow = GlobalV::NLOCAL;
		pv->ncol = GlobalV::NLOCAL;
	}
#ifdef __MPI
    else if(GlobalV::KS_SOLVER=="scalpack" || GlobalV::KS_SOLVER=="genelpa" || GlobalV::KS_SOLVER=="hpseps" 
		|| GlobalV::KS_SOLVER=="selinv" || GlobalV::KS_SOLVER=="scalapack_gvx") //xiaohui add 2013-09-02
    {
        // GlobalV::ofs_running << " nrow=" << nrow << std::endl;
        for (int irow=0; irow< pv->nrow; irow++)
        {
            int global_row = pv->MatrixInfo.row_set[irow];
            pv->trace_loc_row[global_row] = irow;
			// GlobalV::ofs_running << " global_row=" << global_row 
			// << " trace_loc_row=" << pv->trace_loc_row[global_row] << std::endl;
        }

        // GlobalV::ofs_running << " ncol=" << ncol << std::endl;
        for (int icol=0; icol< pv->ncol; icol++)
        {
            int global_col = pv->MatrixInfo.col_set[icol];
            pv->trace_loc_col[global_col] = icol;
			// GlobalV::ofs_running << " global_col=" << global_col 
			// << " trace_loc_col=" << pv->trace_loc_row[global_col] << std::endl;
        }
    }
#endif
    else 
    {
        std::cout << " Parallel Orbial, GlobalV::DIAGO_TYPE = " << GlobalV::KS_SOLVER << std::endl;
        ModuleBase::WARNING_QUIT("ORB_control::set_trace","Check ks_solver.");
    }

    //---------------------------
    // print the trace for test.
    //---------------------------
    /*
    GlobalV::ofs_running << " " << std::setw(10) << "GlobalRow" << std::setw(10) << "LocalRow" << std::endl;
    for(int i=0; i<GlobalV::NLOCAL; i++)
    {
        GlobalV::ofs_running << " " << std::setw(10) << i << std::setw(10) << pv->trace_loc_row[i] << std::endl;

    }

    GlobalV::ofs_running << " " << std::setw(10) << "GlobalCol" << std::setw(10) << "LocalCol" << std::endl;
    for(int j=0; j<GlobalV::NLOCAL; j++)
    {
        GlobalV::ofs_running << " " << std::setw(10) << j << std::setw(10) << trace_loc_col[j] << std::endl;
    }
    */

    return;
}

#ifdef __MPI
inline int cart2blacs(
	MPI_Comm comm_2D,
	int nprows,
	int npcols,
    int Nlocal,
    int Nbands, 
    int nblk,
	int lld,
    int* desc,
    int* desc_wfc)
{
    int my_blacs_ctxt;
    int myprow, mypcol;
    int *usermap=new int[nprows*npcols];
    int info=0;
    for(int i=0; i<nprows; ++i)
    {
        for(int j=0; j<npcols; ++j)
        {
            int pcoord[2]={i, j};
            MPI_Cart_rank(comm_2D, pcoord, &usermap[i+j*nprows]);
        }
    }
    MPI_Fint comm_2D_f = MPI_Comm_c2f(comm_2D);
    Cblacs_get(comm_2D_f, 0, &my_blacs_ctxt);
    Cblacs_gridmap(&my_blacs_ctxt, usermap, nprows, nprows, npcols);
    Cblacs_gridinfo(my_blacs_ctxt, &nprows, &npcols, &myprow, &mypcol);
    delete[] usermap;
    int ISRC=0;
    descinit_(desc, &Nlocal, &Nlocal, &nblk, &nblk, &ISRC, &ISRC, &my_blacs_ctxt, &lld, &info);
    descinit_(desc_wfc, &Nlocal, &Nbands, &nblk, &nblk, &ISRC, &ISRC, &my_blacs_ctxt, &lld, &info);

    return my_blacs_ctxt;
}
#endif

void ORB_control::divide_HS_2d
(
#ifdef __MPI
	MPI_Comm DIAG_WORLD
#endif
)
{
	ModuleBase::TITLE("ORB_control","divide_HS_2d");
	assert(GlobalV::NLOCAL>0);
    assert(GlobalV::DSIZE > 0);
    Parallel_Orbitals* pv = &this->ParaV;

#ifdef __MPI
	DIAG_HPSEPS_WORLD=DIAG_WORLD;
#endif

	if(GlobalV::DCOLOR!=0) return; // mohan add 2012-01-13

	// get the 2D index of computer.
	pv->dim0 = (int)sqrt((double)GlobalV::DSIZE); //mohan update 2012/01/13
	//while (GlobalV::NPROC_IN_POOL%dim0!=0)
	while (GlobalV::DSIZE%pv->dim0!=0)
	{
		pv->dim0 = pv->dim0 - 1;
	}
	assert(pv->dim0 > 0);
	pv->dim1=GlobalV::DSIZE/pv->dim0;

	if(pv->testpb)ModuleBase::GlobalFunc::OUT(GlobalV::ofs_running,"dim0",pv->dim0);
	if(pv->testpb)ModuleBase::GlobalFunc::OUT(GlobalV::ofs_running,"dim1",pv->dim1);

#ifdef __MPI
	// mohan add 2011-04-16
	if(GlobalV::NB2D==0)
	{
		if(GlobalV::NLOCAL>0) pv->nb = 1;
		if(GlobalV::NLOCAL>500) pv->nb = 32;
		if(GlobalV::NLOCAL>1000) pv->nb = 64;
	}
	else if(GlobalV::NB2D>0)
	{
		pv->nb = GlobalV::NB2D; // mohan add 2010-06-28
	}
	ModuleBase::GlobalFunc::OUT(GlobalV::ofs_running,"nb2d", pv->nb);

	this->set_parameters();

	// call mpi_creat_cart
	this->mpi_creat_cart(&pv->comm_2D,pv->dim0,pv->dim1);

	// call mat_2d
	this->mat_2d(pv->comm_2D, GlobalV::NLOCAL, GlobalV::NBANDS, pv->nb, pv->MatrixInfo);

	// mohan add 2010-06-29
	pv->nrow = pv->MatrixInfo.row_num;
	pv->ncol = pv->MatrixInfo.col_num;
	pv->nloc = pv->MatrixInfo.col_num * pv->MatrixInfo.row_num;

	// init blacs context for genelpa
    if(GlobalV::KS_SOLVER=="genelpa" || GlobalV::KS_SOLVER=="scalapack_gvx")
    {
        pv->blacs_ctxt=cart2blacs(pv->comm_2D, pv->dim0, pv->dim1, GlobalV::NLOCAL, GlobalV::NBANDS, pv->nb, pv->nrow, pv->desc, pv->desc_wfc);
    }
#else // single processor used.
	pv->nb = GlobalV::NLOCAL;
	pv->nrow = GlobalV::NLOCAL;
	pv->ncol = GlobalV::NLOCAL;
	pv->nloc = GlobalV::NLOCAL * GlobalV::NLOCAL;
	this->set_parameters();
	pv->MatrixInfo.row_b = 1;
	pv->MatrixInfo.row_num = GlobalV::NLOCAL;
	delete[] pv->MatrixInfo.row_set;
	pv->MatrixInfo.row_set = new int[GlobalV::NLOCAL];
	for(int i=0; i<GlobalV::NLOCAL; i++)
	{
		pv->MatrixInfo.row_set[i]=i;
	}
	pv->MatrixInfo.row_pos=0;

	pv->MatrixInfo.col_b = 1;
	pv->MatrixInfo.col_num = GlobalV::NLOCAL;
	delete[] pv->MatrixInfo.col_set;
	pv->MatrixInfo.col_set = new int[GlobalV::NLOCAL];
	for(int i=0; i<GlobalV::NLOCAL; i++)
	{
		pv->MatrixInfo.col_set[i]=i;
	}
	pv->MatrixInfo.col_pos=0;
#endif

	assert(pv->nloc>0);
	if(pv->testpb)ModuleBase::GlobalFunc::OUT(GlobalV::ofs_running,"MatrixInfo.row_num",pv->MatrixInfo.row_num);
	if(pv->testpb)ModuleBase::GlobalFunc::OUT(GlobalV::ofs_running,"MatrixInfo.col_num",pv->MatrixInfo.col_num);
	if(pv->testpb)ModuleBase::GlobalFunc::OUT(GlobalV::ofs_running,"nloc",pv->nloc);
	return;
}
