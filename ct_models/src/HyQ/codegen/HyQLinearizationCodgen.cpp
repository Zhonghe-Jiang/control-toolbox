/***********************************************************************************
Copyright (c) 2017, Michael Neunert, Markus Giftthaler, Markus Stäuble, Diego Pardo,
Farbod Farshidian. All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of ETH ZURICH nor the names of its contributors may be used
      to endorse or promote products derived from this software without specific
      prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL ETH ZURICH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***************************************************************************************/

#include <ct/core/core.h>
#include <ct/rbd/rbd.h>
#include <ct/models/HyQ/HyQ.h>
#include <ct/models/CodegenOutputDirs.h>

#include "helperFunctions.h"

// shortcut for the auto-diff codegen scalar
typedef CppAD::AD<CppAD::cg::CG<double> > SCALAR;
typedef typename SCALAR::value_type AD_ValueType;

// a floating base forward-dynamic system templated on scalar type
typedef ct::rbd::FloatingBaseFDSystem<ct::rbd::HyQ::tpl::Dynamics<SCALAR>, false> HyQSystemAD;

// extract dimensions
const size_t state_dim = HyQSystemAD::STATE_DIM;
const size_t control_dim = HyQSystemAD::CONTROL_DIM;

// shortcut for number of joints
const size_t njoints = HyQSystemAD::Kinematics::NJOINTS;


void generateInverseDynamics()
{
	typedef ct::core::JacobianCG<state_dim+18, control_dim+6> JacCG;
	typename JacCG::Function f = ct::models::HyQ::hyqInverseDynamics<SCALAR>;
	JacCG jacCG(f);

	try {
		std::cout << "Generating Jacobian of Inverse Dynamics wrt state using forward mode... " << std::endl;
		jacCG.generateCode("HyQInverseDynJacForward", ct::models::HYQ_CODEGEN_OUTPUT_DIR, ct::core::CODEGEN_TEMPLATE_DIR, "models", "HyQ", JacCG::Sparsity::Ones(), false);

		std::cout << "Generating Jacobian of Inverse Dynamics wrt state using reverse mode... " << std::endl;
		jacCG.generateCode("HyQInverseDynJacReverse", ct::models::HYQ_CODEGEN_OUTPUT_DIR, ct::core::CODEGEN_TEMPLATE_DIR, "models", "HyQ", JacCG::Sparsity::Ones(), true);
	} catch (const std::runtime_error& e)
	{
		std::cout << "inverse dynamics code generation failed: "<<e.what()<<std::endl;
	}
}

void generateForwardKinematics()
{
	typedef ct::core::JacobianCG<state_dim, 4*6> JacCG;
	typename JacCG::Function f = ct::models::HyQ::hyqForwardKinematics<SCALAR>;
	JacCG jacCG(f);

	try {
		std::cout << "Generating Jacobian of Forward Kinematics wrt state using forward mode... " << std::endl;
		jacCG.generateCode("HyQForwardKinJacForward", ct::models::HYQ_CODEGEN_OUTPUT_DIR, ct::core::CODEGEN_TEMPLATE_DIR, "models", "HyQ", JacCG::Sparsity::Ones(), false);

		std::cout << "Generating Jacobian of Forward Kinematics wrt state using reverse mode... " << std::endl;
		jacCG.generateCode("HyQForwardKinJacReverse", ct::models::HYQ_CODEGEN_OUTPUT_DIR, ct::core::CODEGEN_TEMPLATE_DIR, "models", "HyQ", JacCG::Sparsity::Ones(), true);
	} catch (const std::runtime_error& e)
	{
		std::cout << "forward kinematics code generation failed: "<<e.what()<<std::endl;
	}
}

void generateForwardZeroForwardDynamics()
{
	typedef ct::core::JacobianCG<state_dim+control_dim + 1, state_dim> JacCG;
	// Eigen::Matrix<SCALAR, state_dim + control_dim + 1, 1> testInput = Eigen::Matrix<SCALAR, state_dim + control_dim + 1, 1>::Random();
	// auto asd = ct::models::HyQ::hyqContactModelForwardDynamics<SCALAR>(testInput);
	typename JacCG::Function f = ct::models::HyQ::hyqContactModelForwardDynamics<SCALAR>;
	JacCG jacCG(f);

	try {
		std::cout << "Generating Forward Zero Code... " << std::endl;
		jacCG.generateForwardZeroCode("HyQForwardZero", ct::models::HYQ_CODEGEN_OUTPUT_DIR, ct::core::CODEGEN_TEMPLATE_DIR, "models", "HyQ", false);
	} catch (const std::runtime_error& e)
	{
		std::cout << "forward zero code generation failed: "<<e.what()<<std::endl;
	}

}


void generateFDLinearization(int argc, char* argv[])
{
	std::cout << "Generating Jacobian of Forward Dynamics... " << std::endl;

	// a contact model (auto-diff'able)
	typedef ct::rbd::EEContactModel<typename HyQSystemAD::Kinematics> ContactModel;

	std::shared_ptr<HyQSystemAD> adSystem = std::shared_ptr<HyQSystemAD>(new HyQSystemAD);

	// explicitely pass kinematics from the system such that both, contact model and system use
	// the same instance to give the AD codegen the opportunity to fully optimize the code
	std::shared_ptr<ContactModel> contactModel = std::shared_ptr<ContactModel> (
			new ContactModel(
					SCALAR(5000.0),
					SCALAR(1000.0),
					SCALAR(100.0),
					SCALAR(100.0),
					SCALAR(-0.02),
					ContactModel::VELOCITY_SMOOTHING::SIGMOID,
					adSystem->dynamics().kinematicsPtr())
	);

	bool useContactModel = (argc<=2 || !std::string(argv[2]).compare("nocontact") == 0);
	std::cout << std::boolalpha << "using contact model: "<<useContactModel<<std::endl;

	// asign the contact model
	if (useContactModel)
		adSystem->setContactModel(contactModel);

	// create the codegen linearizer
	ct::core::ADCodegenLinearizer<state_dim, control_dim> adLinearizer(adSystem);

	std::string baseName;
	if (useContactModel)
		baseName = "HyQWithContactModelLinearized";
	else
		baseName = "HyQBareModelLinearized";

	try {
		std::cout<< "generating code..." << std::endl;
		if (argc>1 && std::string(argv[1]).compare("reverse") == 0)
		{
			std::cout << "generating using reverse mode" << std::endl;

			adLinearizer.generateCode(
					baseName+"Reverse",
					ct::models::HYQ_CODEGEN_OUTPUT_DIR,
					ct::core::CODEGEN_TEMPLATE_DIR,
					"models",
					"HyQ",
					true);
		} else
		{
			std:: cout << "generating using forward mode" << std::endl;

			adLinearizer.generateCode(
					baseName+"Forward",
					ct::models::HYQ_CODEGEN_OUTPUT_DIR,
					ct::core::CODEGEN_TEMPLATE_DIR,
					"models",
					"HyQ",
					false);
		}

		std::cout<< "... done!" << std::endl;
	} catch (const std::runtime_error& e)
	{
		std::cout << "forward dynamics code generation failed: "<<e.what()<<std::endl;
	}
}


int main(int argc, char* argv[])
{
	generateFDLinearization(argc, argv);
	generateInverseDynamics();
	generateForwardKinematics();
	generateForwardZeroForwardDynamics();
}

