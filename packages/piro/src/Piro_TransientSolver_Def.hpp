// @HEADER
// ************************************************************************
//
//        Piro: Strategy package for embedded analysis capabilitites
//                  Copyright (2010) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Andy Salinger (agsalin@sandia.gov), Sandia
// National Laboratories.
//
// ************************************************************************
// @HEADER

#include "Piro_TransientSolver.hpp"

#include "Teuchos_ScalarTraits.hpp"
#include "Teuchos_Array.hpp"
#include "Teuchos_Tuple.hpp"
#include "Teuchos_XMLParameterListHelpers.hpp"
#include "Teuchos_Assert.hpp"

#include "Thyra_DefaultAddedLinearOp.hpp"
#include "Thyra_DefaultMultipliedLinearOp.hpp"
#include "Thyra_DefaultZeroLinearOp.hpp"
#include "Thyra_VectorStdOps.hpp"
#include "Thyra_DefaultModelEvaluatorWithSolveFactory.hpp"

#define DEBUG_OUTPUT

#include <string>
#include <stdexcept>
#include <iostream>

template <typename Scalar>
Piro::TransientSolver<Scalar>::TransientSolver(
  const Teuchos::RCP<Thyra::ModelEvaluator<Scalar> > &model, 
  const Teuchos::RCP<Thyra::ModelEvaluator<Scalar> > &icModel):
  out_(Teuchos::VerboseObjectBase::getDefaultOStream()),
  model_(model), 
  initialConditionModel_(icModel),
  num_p_(model->Np()), 
  num_g_(model->Ng())
{
#ifdef DEBUG_OUTPUT
  *out_ << "DEBUG: " << __PRETTY_FUNCTION__ << "\n";
#endif
}

template <typename Scalar>
Piro::TransientSolver<Scalar>::TransientSolver(
    const Teuchos::RCP<Thyra::ModelEvaluator<Scalar> > &model, int numParameters, 
    const Teuchos::RCP<Thyra::ModelEvaluator<Scalar> > &icModel) : 
    out_(Teuchos::VerboseObjectBase::getDefaultOStream()),
    model_(model),
    initialConditionModel_(icModel),
    num_p_(numParameters),
    num_g_(model->Ng()),
    sensitivityMethod_(NONE) 
{
#ifdef DEBUG_OUTPUT
  *out_ << "DEBUG: " << __PRETTY_FUNCTION__ << "\n";
#endif
}

template<typename Scalar>
Teuchos::RCP<const Thyra::VectorSpaceBase<Scalar> >
Piro::TransientSolver<Scalar>::get_p_space(int l) const
{
#ifdef DEBUG_OUTPUT
  *out_ << "DEBUG: " << __PRETTY_FUNCTION__ << "\n";
#endif
  TEUCHOS_TEST_FOR_EXCEPTION(
      l >= num_p_ || l < 0,
      Teuchos::Exceptions::InvalidParameter,
      "\n Error in Piro::TransientSolver::get_p_map():  " <<
      "Invalid parameter index l = " <<
      l << "\n");

  return model_->get_p_space(l);
}

template<typename Scalar>
Teuchos::RCP<const Thyra::VectorSpaceBase<Scalar> >
Piro::TransientSolver<Scalar>::get_g_space(int j) const
{
#ifdef DEBUG_OUTPUT
  *out_ << "DEBUG: " << __PRETTY_FUNCTION__ << "\n";
#endif
  TEUCHOS_TEST_FOR_EXCEPTION(
      j > num_g_ || j < 0,
      Teuchos::Exceptions::InvalidParameter,
      "\n Error in Piro::TransientSolver::get_g_map():  " <<
      "Invalid response index j = " <<
      j << "\n");

  if (j < num_g_) {
    return model_->get_g_space(j);
  } else {
    // j == num_g_
    return model_->get_x_space();
  }
}

template<typename Scalar>
Thyra::ModelEvaluatorBase::InArgs<Scalar>
Piro::TransientSolver<Scalar>::getNominalValues() const
{
#ifdef DEBUG_OUTPUT
  *out_ << "DEBUG: " << __PRETTY_FUNCTION__ << "\n";
#endif
  Thyra::ModelEvaluatorBase::InArgs<Scalar> result = this->createInArgs();
  const Thyra::ModelEvaluatorBase::InArgs<Scalar> modelNominalValues = model_->getNominalValues();
  for (int l = 0; l < num_p_; ++l) {
    result.set_p(l, modelNominalValues.get_p(l));
  }
  return result;
}

template <typename Scalar>
Thyra::ModelEvaluatorBase::InArgs<Scalar>
Piro::TransientSolver<Scalar>::createInArgs() const
{
#ifdef DEBUG_OUTPUT
  *out_ << "DEBUG: " << __PRETTY_FUNCTION__ << "\n";
#endif
  Thyra::ModelEvaluatorBase::InArgsSetup<Scalar> inArgs;
  inArgs.setModelEvalDescription(this->description());
  inArgs.set_Np(num_p_);
  return inArgs;
}

template <typename Scalar>
Thyra::ModelEvaluatorBase::OutArgs<Scalar>
Piro::TransientSolver<Scalar>::createOutArgsImpl() const
{
#ifdef DEBUG_OUTPUT
  *out_ << "DEBUG: " << __PRETTY_FUNCTION__ << "\n";
  *out_ << "DEBUG num_p_, num_g_ = " << num_p_ << ", " << num_g_ << "\n";  
#endif
  Thyra::ModelEvaluatorBase::OutArgsSetup<Scalar> outArgs;
  outArgs.setModelEvalDescription(this->description());

  // One additional response slot for the solution vector
  outArgs.set_Np_Ng(num_p_, num_g_ + 1);

  const Thyra::ModelEvaluatorBase::OutArgs<Scalar> modelOutArgs = model_->createOutArgs();

  if (num_p_ > 0) {
    // Only one parameter supported
    const int l = 0;

    if (Teuchos::nonnull(initialConditionModel_)) {
      const Thyra::ModelEvaluatorBase::OutArgs<Scalar> initCondOutArgs =
        initialConditionModel_->createOutArgs();
      const Thyra::ModelEvaluatorBase::DerivativeSupport init_dxdp_support =
        initCondOutArgs.supports(Thyra::ModelEvaluatorBase::OUT_ARG_DgDp, initCondOutArgs.Ng() - 1, l);
      if (!init_dxdp_support.supports(Thyra::ModelEvaluatorBase::DERIV_MV_JACOBIAN_FORM)) {
#ifdef DEBUG_OUTPUT
        *out_ << "DEBUG: init_dxdp_support = DERIV_MV_JACOBIAN_FORM\n"; 
#endif
        // Ok to return early since only one parameter supported
        return outArgs;
      }
    }

    // IKT, 5/6/2020: the following should not be needed for transient forward sensitivities
    // but keeping for now.
    //
    // Computing the DxDp sensitivity for a transient problem currently requires the evaluation of
    // the mutilivector-based, Jacobian-oriented DfDp derivatives of the underlying transient model.
    const Thyra::ModelEvaluatorBase::DerivativeSupport model_dfdp_support =
      modelOutArgs.supports(Thyra::ModelEvaluatorBase::OUT_ARG_DfDp, l);
    if (!model_dfdp_support.supports(Thyra::ModelEvaluatorBase::DERIV_MV_JACOBIAN_FORM)) {
      // Ok to return early since only one parameter supported
      return outArgs;
    }

    // Solution sensitivity
    outArgs.setSupports(
        Thyra::ModelEvaluatorBase::OUT_ARG_DgDp,
        num_g_,
        l,
        Thyra::ModelEvaluatorBase::DERIV_MV_JACOBIAN_FORM);

#ifdef DEBUG_OUTPUT
        *out_ << "DEBUG: dgdp_support = DERIV_MV_JACOBIAN_FORM\n"; 
#endif

    if (num_g_ > 0) {
      // Only one response supported
      const int j = 0;

      const Thyra::ModelEvaluatorBase::DerivativeSupport model_dgdx_support =
        modelOutArgs.supports(Thyra::ModelEvaluatorBase::OUT_ARG_DgDx, j);
      if (!model_dgdx_support.none()) {
        const Thyra::ModelEvaluatorBase::DerivativeSupport model_dgdp_support =
          modelOutArgs.supports(Thyra::ModelEvaluatorBase::OUT_ARG_DgDp, j, l);
        // Response sensitivity
        Thyra::ModelEvaluatorBase::DerivativeSupport dgdp_support;
        if (model_dgdp_support.supports(Thyra::ModelEvaluatorBase::DERIV_MV_JACOBIAN_FORM)) {
          dgdp_support.plus(Thyra::ModelEvaluatorBase::DERIV_MV_JACOBIAN_FORM);
#ifdef DEBUG_OUTPUT
          *out_ << "DEBUG: dgdp_support = DERIV_MV_JACOBIAN_FORM\n"; 
#endif
        }
        if (model_dgdp_support.supports(Thyra::ModelEvaluatorBase::DERIV_LINEAR_OP)) {
          dgdp_support.plus(Thyra::ModelEvaluatorBase::DERIV_LINEAR_OP);
#ifdef DEBUG_OUTPUT
          *out_ << "DEBUG: dgdp_support = DERIV_LINEAR_OP\n"; 
#endif
        }
        outArgs.setSupports(
            Thyra::ModelEvaluatorBase::OUT_ARG_DgDp,
            j,
            l,
            dgdp_support);
      }
    }
  }

  return outArgs;
}


template <typename Scalar>
Teuchos::RCP<Thyra::LinearOpBase<Scalar> >
Piro::TransientSolver<Scalar>::create_DgDp_op_impl(int j, int l) const
{
  TEUCHOS_ASSERT(j != num_g_);
  const Teuchos::Array<Teuchos::RCP<const Thyra::LinearOpBase<Scalar> > > dummy =
    Teuchos::tuple(Thyra::zero<Scalar>(this->get_g_space(j), this->get_p_space(l)));
  return Teuchos::rcp(new Thyra::DefaultAddedLinearOp<Scalar>(dummy));
}


template <typename Scalar>
const Thyra::ModelEvaluator<Scalar> &
Piro::TransientSolver<Scalar>::getModel() const 
{
  return model_; 
}

template <typename Scalar>
int 
Piro::TransientSolver<Scalar>::num_p() const 
{
  return num_p_; 
}

template <typename Scalar> 
int 
Piro::TransientSolver<Scalar>::num_g() const 
{
  return num_g_; 
}

template <typename Scalar>
void 
Piro::TransientSolver<Scalar>::setSensitivityMethod(const std::string sensitivity_method_string)
{
  if (sensitivity_method_string == "None") sensitivityMethod_ = NONE; 
  else if (sensitivity_method_string == "Forward") sensitivityMethod_ = FORWARD;
  else if (sensitivity_method_string == "Adjoint") sensitivityMethod_ = ADJOINT;
  else {
    TEUCHOS_TEST_FOR_EXCEPTION(
        true,
        Teuchos::Exceptions::InvalidParameter,
        "\n Error! Piro::TransientSolver: invalid Sensitivity Method = " << sensitivity_method_string << "! \n" 
        << " Valid options for Sensitivity Method are 'None', 'Forward' and 'Adjoint'.\n");
  }
  //IKT, 5/8/2020: remove the following once we have support for adjoint sensitivities 
  if (sensitivityMethod_ == ADJOINT) {
    TEUCHOS_TEST_FOR_EXCEPTION(
        true,
        Teuchos::Exceptions::InvalidParameter,
        "\n Error! Piro::TransientSolver: adjoint sentivities (Sensitivity Method = "
        << "Adjoint) are not yet supported!  Please set 'Sensitivity Method' to 'None' or 'Forward'.\n");
  }
}

template <typename Scalar>
int 
Piro::TransientSolver<Scalar>::getSensitivityMethod()
{
  //The correspondance b/w the enum SENS_METHOD and int is as follows:
  //NONE = 0, FORWARD = 1, ADJOINT = 2 
  return sensitivityMethod_; 
}

template <typename Scalar>
void 
Piro::TransientSolver<Scalar>::evalConvergedModel(
      const Thyra::ModelEvaluatorBase::InArgs<Scalar>& modelInArgs,
      const Thyra::ModelEvaluatorBase::OutArgs<Scalar>& outArgs) const
{
#ifdef DEBUG_OUTPUT
  *out_ << "DEBUG: " << __PRETTY_FUNCTION__ << "\n";
  *out_ << "DEBUG sensitivityMethod = " << sensitivityMethod_ << "\n"; 
#endif
  using Teuchos::RCP;
  using Teuchos::rcp;
  
  // Check if sensitivities are requested 
  bool requestedSensitivities = false;
  for (int i=0; i<num_p_; i++) {
    for (int j=0; j<=num_g_; j++) {
      if (!outArgs.supports(Thyra::ModelEvaluatorBase::OUT_ARG_DgDp, j, i).none() && !outArgs.get_DgDp(j,i).isEmpty()) {
        requestedSensitivities = true;
        break;
      }
    }
  }

#ifdef DEBUG_OUTPUT
  *out_ << "DEBUG requestedSensitivities = " << requestedSensitivities << "\n";
#endif 
  if (requestedSensitivities == true) {

    if (sensitivityMethod_ == NONE) {

      //If sensitivities are requested but 'Sensitivity Method' is set to 'None', throw an error.
      TEUCHOS_TEST_FOR_EXCEPTION(
          true,
          Teuchos::Exceptions::InvalidParameter,
          "\n Error! Piro::TransientSolver: you have specified 'Sensitivity Method = None' yet the model supports suggest " 
          << "sensitivities are requested.  Please change 'Sensitivity Method' to 'Forward' or 'Adjoint'\n");
    }
    //
    *out_ << "\nE) Solve the forward problem with Sensitivities...\n";
    //
    TEUCHOS_TEST_FOR_EXCEPTION(
        true,
        Teuchos::Exceptions::InvalidParameter,
        "\n Error! Piro::TransientSolver: sensitivities with Tempus are not yet supported!");
  }

  *out_ << "\nF) Calculate responses ...\n";

  // Deal with responses 
  Thyra::ModelEvaluatorBase::OutArgs<Scalar> modelOutArgs = model_->createOutArgs();
  for (int j=0; j<num_g_; j++) { 
    auto g_out = outArgs.get_g(j); 
    if (g_out != Teuchos::null) {
      Thyra::put_scalar(Teuchos::ScalarTraits<Scalar>::zero(), g_out.ptr());
      modelOutArgs.set_g(j, g_out);
    }

    //IKT, 5/6/2020: not sure if the following is needed.
    // Jacobian
    bool jacobianRequired = false;
    for (int j = 0; j <= num_g_; ++j) { // resize
      for (int l = 0; l < num_p_; ++l) {
        const Thyra::ModelEvaluatorBase::DerivativeSupport dgdp_support =
          outArgs.supports(Thyra::ModelEvaluatorBase::OUT_ARG_DgDp, j, l);
        if (!dgdp_support.none()) {
          const Thyra::ModelEvaluatorBase::Derivative<Scalar> dgdp_deriv =
              outArgs.get_DgDp(j, l);
          if (!dgdp_deriv.isEmpty()) 
          {
            jacobianRequired = true;
          }
        }
      }
      if (jacobianRequired) {
        const RCP<Thyra::LinearOpWithSolveBase<Scalar> > jacobian = model_->create_W();
        modelOutArgs.set_W(jacobian);
      }
    }
  }
    
  // DgDx derivatives
  for (int j = 0; j < num_g_; ++j) {
    Thyra::ModelEvaluatorBase::DerivativeSupport dgdx_request;
    for (int l = 0; l < num_p_; ++l) {
      const Thyra::ModelEvaluatorBase::DerivativeSupport dgdp_support =
        outArgs.supports(Thyra::ModelEvaluatorBase::OUT_ARG_DgDp, j, l);
      if (!dgdp_support.none()) {
        const Thyra::ModelEvaluatorBase::Derivative<Scalar> dgdp_deriv =
          outArgs.get_DgDp(j, l);
        if (!dgdp_deriv.isEmpty()) {
          const bool dgdp_mvGrad_required =
            Teuchos::nonnull(dgdp_deriv.getMultiVector()) &&
            dgdp_deriv.getMultiVectorOrientation() == Thyra::ModelEvaluatorBase::DERIV_MV_GRADIENT_FORM;
          if (dgdp_mvGrad_required) {
            dgdx_request.plus(Thyra::ModelEvaluatorBase::DERIV_MV_GRADIENT_FORM);
          } 
          else {
            dgdx_request.plus(Thyra::ModelEvaluatorBase::DERIV_LINEAR_OP);
          }
        }
      }
    }

    if (!dgdx_request.none()) {
      Thyra::ModelEvaluatorBase::Derivative<Scalar> dgdx_deriv;
      if (dgdx_request.supports(Thyra::ModelEvaluatorBase::DERIV_MV_GRADIENT_FORM)) {
        dgdx_deriv = Thyra::create_DgDx_mv(*model_, j, Thyra::ModelEvaluatorBase::DERIV_MV_GRADIENT_FORM);
      } 
      else if (dgdx_request.supports(Thyra::ModelEvaluatorBase::DERIV_LINEAR_OP)) {
        dgdx_deriv = model_->create_DgDx_op(j);
      }
      modelOutArgs.set_DgDx(j, dgdx_deriv);
    }
  }
    
  // DgDp derivatives
  for (int l = 0; l < num_p_; ++l) {
    for (int j = 0; j < num_g_; ++j) {
      const Thyra::ModelEvaluatorBase::DerivativeSupport dgdp_support =
        outArgs.supports(Thyra::ModelEvaluatorBase::OUT_ARG_DgDp, j, l);
      if (!dgdp_support.none()) {
        const Thyra::ModelEvaluatorBase::Derivative<Scalar> dgdp_deriv =
            outArgs.get_DgDp(j, l);
        Thyra::ModelEvaluatorBase::Derivative<Scalar> model_dgdp_deriv;
        const RCP<Thyra::LinearOpBase<Scalar> > dgdp_op = dgdp_deriv.getLinearOp();
        if (Teuchos::nonnull(dgdp_op)) {
          model_dgdp_deriv = model_->create_DgDp_op(j, l);
        } 
        else {
          model_dgdp_deriv = dgdp_deriv;
        }
        if (!model_dgdp_deriv.isEmpty()) {
          modelOutArgs.set_DgDp(j, l, model_dgdp_deriv);
        }
      }
    }
  }
    
  model_->evalModel(modelInArgs, modelOutArgs);

  // Return the final solution as an additional g-vector, if requested
  RCP<Thyra::VectorBase<Scalar> > gx_out = outArgs.get_g(num_g_);
  if (Teuchos::nonnull(gx_out)) {
    Thyra::copy(*modelInArgs.get_x(), gx_out.ptr());
  }
}
