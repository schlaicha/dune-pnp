#include <dune/geometry/referenceelements.hh> 
#include <dune/geometry/quadraturerules.hh>
#include <dune/pdelab/common/geometrywrapper.hh>
#include <dune/pdelab/localoperator/defaultimp.hh>
#include <dune/pdelab/localoperator/pattern.hh>
#include <dune/pdelab/localoperator/flags.hh>

/** a local operator for solving the equation
 *
 *   - \Delta u + a*u = f   in \Omega
 *                  u = g   on \Gamma_D\subseteq\partial\Omega
 *  -\nabla u \cdot n = j   on \Gamma_N = \partial\Omega\setminus\Gamma_D
 *
 * with conforming finite elements on all types of grids in any dimension
 *
 * \tparam B a function indicating the type of boundary condition
 */
template<class B, class F, class S, class PhiGFS, class UPhi>
class DiffusionOperator : 
  public Dune::PDELab::NumericalJacobianApplyVolume   <DiffusionOperator<B, F, S, PhiGFS, UPhi> >,
  public Dune::PDELab::NumericalJacobianVolume        <DiffusionOperator<B, F, S, PhiGFS, UPhi> >,
  public Dune::PDELab::NumericalJacobianApplyBoundary <DiffusionOperator<B, F, S, PhiGFS, UPhi> >,
  public Dune::PDELab::NumericalJacobianBoundary      <DiffusionOperator<B, F, S, PhiGFS, UPhi> >,
  public Dune::PDELab::FullVolumePattern,
  public Dune::PDELab::LocalOperatorDefaultFlags,
  public Dune::PDELab::InstationaryLocalOperatorDefaultMethods<double> // default methods
{
public:
  // pattern assembly flags
  enum { doPatternVolume = true };

  // residual assembly flags
  enum { doAlphaVolume = true };
  enum { doAlphaBoundary = false };                                // assemble boundary
  
  DiffusionOperator (const B& b_, const F& f_, S& s_, PhiGFS & phiGFS_, UPhi & uphi_, double valency_, unsigned int intorder_=2)  // needs boundary cond. type
    : b(b_), f(f_), intorder(intorder_), s(s_), phiGFS(phiGFS_), uphi(uphi_), valency(valency_)
  {}

  // volume integral depending on test and ansatz functions
  template<typename EG, typename LFSU, typename X, typename LFSV, typename R>
  void alpha_volume (const EG& eg, const LFSU& lfsu, const X& x, const LFSV& lfsv, R& r) const
  {
    // extract some types
    typedef typename LFSU::Traits::FiniteElementType::
      Traits::LocalBasisType::Traits::DomainFieldType DF;
    typedef typename LFSU::Traits::FiniteElementType::
      Traits::LocalBasisType::Traits::RangeFieldType RF;
    typedef typename LFSU::Traits::FiniteElementType::
      Traits::LocalBasisType::Traits::JacobianType JacobianType;
    typedef typename LFSU::Traits::FiniteElementType::
      Traits::LocalBasisType::Traits::RangeType RangeType;
    typedef typename LFSU::Traits::SizeType size_type;
        
    // dimensions
    const int dim = EG::Geometry::dimension;
    const int dimw = EG::Geometry::dimensionworld;

    // select quadrature rule
    Dune::GeometryType gt = eg.geometry().type();
    const Dune::QuadratureRule<DF,dim>& 
      rule = Dune::QuadratureRules<DF,dim>::rule(gt,intorder);

    // loop over quadrature points
    for (typename Dune::QuadratureRule<DF,dim>::const_iterator 
           it=rule.begin(); it!=rule.end(); ++it)
      {
        // evaluate basis functions on reference element
        std::vector<RangeType> phi(lfsu.size());
        lfsu.finiteElement().localBasis().evaluateFunction(it->position(),phi);

        // compute u at integration point
        RF u=0.0;
        for (size_type i=0; i<lfsu.size(); i++)
          u += x[i]*phi[i];

        // evaluate gradient of basis functions on reference element
        std::vector<JacobianType> js(lfsu.size());
        lfsu.finiteElement().localBasis().evaluateJacobian(it->position(),js);

        // transform gradients from reference element to real element
        const Dune::FieldMatrix<DF,dimw,dim> 
          jac = eg.geometry().jacobianInverseTransposed(it->position());
        std::vector<Dune::FieldVector<RF,dim> > gradphi(lfsu.size());
        for (size_type i=0; i<lfsu.size(); i++)
          jac.mv(js[i][0],gradphi[i]);
        
        // compute gradient of u
        Dune::FieldVector<RF,dim> gradu(0.0);
        for (size_type i=0; i<lfsu.size(); i++)
          gradu.axpy(x[i],gradphi[i]);

        // evaluate parameters; 
        Dune::FieldVector<RF,dim> 
          globalpos = eg.geometry().global(it->position());
        RF F = 0; 
        RF a = 0; 

        // integrate grad u * grad phi_i + a*u*phi_i - f phi_i
        RF factor = it->weight()*eg.geometry().integrationElement(it->position());
        Dune::FieldVector<RF,dim> gradPhi;

        typename Dune::PDELab::DiscreteGridFunctionGradient
             < PhiGFS, UPhi > grads(phiGFS,uphi);
         grads.evaluate(eg.entity(), it->position(), gradPhi);



        for (size_type i=0; i<lfsu.size(); i++)
          r[i] += ( gradu*gradphi[i]+u*valency*(gradPhi*gradphi[i]) + a*u*phi[i]  )*factor;
      }
  }

  // boundary integral
  template<typename IG, typename LFSU, typename X, typename LFSV, typename R>
  void alpha_boundary (const IG& ig, const LFSU& lfsu_s, const X& x_s, 
                       const LFSV& lfsv_s, R& r_s) const
  {
    // some types
/*    typedef typename LFSV::Traits::FiniteElementType::
      Traits::LocalBasisType::Traits::DomainFieldType DF;
    typedef typename LFSV::Traits::FiniteElementType::
      Traits::LocalBasisType::Traits::RangeFieldType RF;
    typedef typename LFSV::Traits::FiniteElementType::
      Traits::LocalBasisType::Traits::RangeType RangeType;
    typedef typename LFSV::Traits::SizeType size_type;
        
    // dimensions
    const int dim = IG::dimension;
        
    // select quadrature rule for face
    Dune::GeometryType gtface = ig.geometryInInside().type();
    const Dune::QuadratureRule<DF,dim-1>& 
      rule = Dune::QuadratureRules<DF,dim-1>::rule(gtface,intorder);

    // loop over quadrature points and integrate normal flux
    for (typename Dune::QuadratureRule<DF,dim-1>::const_iterator it=rule.begin(); 
	 it!=rule.end(); ++it)
      {
        // evaluate boundary condition type
        typename B::Traits::RangeType bctype;
        b.evaluate(ig,it->position(),bctype);
 
        // skip rest if we are on Dirichlet boundary
        if (bctype>0) continue;

        // position of quadrature point in local coordinates of element 
        Dune::FieldVector<DF,dim> local = ig.geometryInInside().global(it->position());

        // evaluate basis functions at integration point
        std::vector<RangeType> phi(lfsv_s.size());
        lfsu_s.finiteElement().localBasis().evaluateFunction(local,phi);

        // evaluate u (e.g. flux may depend on u)
        RF u=0.0;
        for (size_type i=0; i<lfsu_s.size(); i++)
          u += x_s[i]*phi[i];
            
        // evaluate flux boundary condition
        RF j;
        j = fluxContainer[ig.intersection().boundarySegmentIndex()];
            
        // integrate j
        RF factor = it->weight()*ig.geometry().integrationElement(it->position());
        for (size_type i=0; i<lfsv_s.size(); i++)
          r_s[i] += j*phi[i]*factor;
      }*/
  }

private:
  const B& b;
  const F& f;
  const S& s;
  unsigned int intorder;
  double valency;
  PhiGFS & phiGFS; 
  UPhi & uphi;
};
