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


namespace ct{
namespace optcon{


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::startupRoutine()
{
	launchWorkerThreads();
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::shutdownRoutine()
{
	workersActive_ = false;
	workerTask_ = SHUTDOWN;
	workerWakeUpCondition_.notify_all();

#ifdef DEBUG_PRINT_MP
	std::cout<<"Shutting down workers"<<std::endl;
#endif // DEBUG_PRINT_MP

	for (size_t i=0; i<workerThreads_.size(); i++)
	{
		workerThreads_[i].join();
	}

#ifdef DEBUG_PRINT_MP
	std::cout<<"All workers shut down"<<std::endl;
#endif // DEBUG_PRINT_MP
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::threadWork(size_t threadId)
{
#ifdef DEBUG_PRINT_MP
	printString("[Thread "+std::to_string(threadId)+"]: launched");
#endif // DEBUG_PRINT_MP


	// local variables
	int workerTask_local = IDLE;
	size_t uniqueProcessID = 0;
	size_t iteration_local = this->iteration_;


	while(workersActive_)
	{
		workerTask_local = workerTask_.load();
		iteration_local = this->iteration_;

#ifdef DEBUG_PRINT_MP
		printString("[Thread " + std::to_string(threadId) + "]: previous procId: " + std::to_string(uniqueProcessID) +
				", current procId: " +std::to_string(generateUniqueProcessID(iteration_local, (int) workerTask_local)));
#endif


		/*!
		 * We want to put the worker to sleep if
		 * - the workerTask_ is IDLE
		 * - or we are finished but workerTask_ is not yet reset, thus the process ID is still the same
		 * */
		if (workerTask_local == IDLE || uniqueProcessID == generateUniqueProcessID(iteration_local, (int) workerTask_local))
		{
#ifdef DEBUG_PRINT_MP
			printString("[Thread " + std::to_string(threadId) + "]: going to sleep !");
#endif

			// sleep until the state is not IDLE any more and we have a different process ID than before
			std::unique_lock<std::mutex> waitLock(workerWakeUpMutex_);
			while(workerTask_ == IDLE ||  (uniqueProcessID == generateUniqueProcessID(this->iteration_, (int)workerTask_.load()))){
				workerWakeUpCondition_.wait(waitLock);
			}
			waitLock.unlock();

			workerTask_local = workerTask_.load();
			iteration_local = this->iteration_;

#ifdef DEBUG_PRINT_MP
			printString("[Thread " + std::to_string(threadId) + "]: woke up !");
#endif // DEBUG_PRINT_MP
		}

		if (!workersActive_)
			break;


		switch(workerTask_local)
		{
		case LINE_SEARCH:
		{
#ifdef DEBUG_PRINT_MP
			printString("[Thread " + std::to_string(threadId) + "]: now busy with LINE_SEARCH !");
#endif // DEBUG_PRINT_MP
			lineSearchWorker(threadId);
			uniqueProcessID = generateUniqueProcessID (iteration_local, LINE_SEARCH);
			break;
		}
		case ROLLOUT_SHOTS:
		{
#ifdef DEBUG_PRINT_MP
			printString("[Thread " + std::to_string(threadId) + "]: now doing shot rollouts !");
#endif // DEBUG_PRINT_MP
			rolloutShotWorker(threadId);
			uniqueProcessID = generateUniqueProcessID (iteration_local, ROLLOUT_SHOTS);
			break;
		}
		case LINEARIZE_DYNAMICS:
		{
#ifdef DEBUG_PRINT_MP
			printString("[Thread " + std::to_string(threadId) + "]: now doing linearization !");
#endif // DEBUG_PRINT_MP
			computeLinearizedDynamicsWorker(threadId);
			uniqueProcessID = generateUniqueProcessID (iteration_local, LINEARIZE_DYNAMICS);
			break;
		}
		case COMPUTE_COST:
		{
#ifdef DEBUG_PRINT_MP
			printString("[Thread " + std::to_string(threadId) + "]: now doing cost computation !");
#endif // DEBUG_PRINT_MP
			computeQuadraticCostsWorker(threadId);
			uniqueProcessID = generateUniqueProcessID (iteration_local, COMPUTE_COST);
			break;
		}
		//		case PARALLEL_BACKWARD_PASS:
		//		{
		//			if (threadId < this->settings_.nThreads-1)
		//			{
		//#ifdef DEBUG_PRINT_MP
		//			std::cout<<"[Thread "<<threadId<<"]: now doing LQ problem building!"<<std::endl;
		//#endif // DEBUG_PRINT_MP
		//				computeLQProblemWorker(threadId);
		//			}
		//			lastCompletedTask = PARALLEL_BACKWARD_PASS;
		//			break;
		//		}
		case SHUTDOWN:
		{
#ifdef DEBUG_PRINT_MP
			printString("[Thread " + std::to_string(threadId) + "]: now shutting down !");
#endif // DEBUG_PRINT_MP
			return;
			break;
		}
		case IDLE:
		{
#ifdef DEBUG_PRINT_MP
			printString("[Thread " + std::to_string(threadId) + "]: is idle. Now going to sleep.");
#endif // DEBUG_PRINT_MP
			break;
		}
		default:
		{
			printString("[Thread " + std::to_string(threadId) + "]: WARNING: Worker has unknown task !");
			break;
		}
		}
	}
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::launchWorkerThreads()
{
	workersActive_ = true;
	workerTask_ = IDLE;

	for (int i=0; i < this->settings_.nThreads; i++)
	{
		workerThreads_.push_back(std::thread(&NLOCBackendMP::threadWork, this, i));
	}
}



//template <size_t STATE_DIM, size_t CONTROL_DIM, typename SCALAR>
//void iLQGMP<STATE_DIM, CONTROL_DIM, SCALAR>::createLQProblem()
//{
//	if (this->settings_.parallelBackward.enabled)
//		parallelLQProblem();
//	else
//		this->sequentialLQProblem();
//}


//template <size_t STATE_DIM, size_t CONTROL_DIM, typename SCALAR>
//void iLQGMP<STATE_DIM, CONTROL_DIM, SCALAR>::parallelLQProblem()
//{
//	Eigen::setNbThreads(1); // disable Eigen multi-threading
//
//	kTaken_ = 0;
//	kCompleted_ = 0;
//	KMax_ = this->K_;
//
//#ifdef DEBUG_PRINT_MP
//	std::cout<<"[MP]: Waking up workers to do parallel backward pass. Will continue immediately"<<std::endl;
//#endif //DEBUG_PRINT_MP
//	workerTask_ = PARALLEL_BACKWARD_PASS;
//	workerWakeUpCondition_.notify_all();
//}


//template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
//void iLQGMP<STATE_DIM, CONTROL_DIM, SCALAR>::backwardPass()
//{
//	// step 3
//	// initialize cost to go (described in step 3)
//	this->initializeCostToGo();
//
//	if (this->settings_.parallelBackward.enabled)
//	{
//		while (kCompleted_ < this->settings_.nThreads*2 && kCompleted_ < this->K_)
//		{
//			if (this->settings_.parallelBackward.showWarnings)
//			{
//				std::cout << "backward pass waiting for head start" << std::endl;
//			}
//			std::this_thread::sleep_for(std::chrono::microseconds(this->settings_.parallelBackward.pollingTimeoutUs));
//		}
//	}
//
//	for (int k=this->K_-1; k>=0; k--) {
//
//		if (this->settings_.parallelBackward.enabled)
//		{
//			while ((this->K_-1 - k + this->settings_.nThreads*2 > kCompleted_) && (k >= this->settings_.nThreads*2))
//			{
//				if (this->settings_.parallelBackward.showWarnings)
//				{
//					std::cout << "backward pass waiting for LQ problems" << std::endl;
//				}
//				std::this_thread::sleep_for(std::chrono::microseconds(this->settings_.parallelBackward.pollingTimeoutUs));
//			}
//		}
//
//#ifdef DEBUG_PRINT_MP
//		if (k%100 == 0)
//			std::cout<<"[MP]: Solving backward pass for index k "<<k<<std::endl;
//#endif
//
//		// design controller
//		this->designController(k);
//
//		// compute cost to go
//		this->computeCostToGo(k);
//	}
//
//	workerTask_ = IDLE;
//}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::computeLinearizedDynamicsAroundTrajectory(size_t firstIndex, size_t lastIndex)
{
	/*!
	 * In special cases, this function may be called for a single index, e.g. for the unconstrained GNMS real-time iteration scheme.
	 * Then, don't wake up workers, but do single-threaded computation for that single index, and return.
	 */
	if(lastIndex == firstIndex)
	{
#ifdef DEBUG_PRINT_MP
		printString("[MP]: do single threaded linearization for single index "+ std::to_string(firstIndex) + ". Not waking up workers.");
#endif //DEBUG_PRINT_MP
		this->computeLinearizedDynamics(this->settings_.nThreads, firstIndex);
		return;
	}


	/*!
	 * In case of multiple points to be linearized, start multi-threading:
	 */
	Eigen::setNbThreads(1); // disable Eigen multi-threading
#ifdef DEBUG_PRINT_MP
	printString("[MP]: Restricting Eigen to " + std::to_string(Eigen::nbThreads()) + " threads.");
#endif //DEBUG_PRINT_MP

	kTaken_ = 0;
	kCompleted_ = 0;
	KMax_ = lastIndex;
	KMin_ = firstIndex;

#ifdef DEBUG_PRINT_MP
	printString("[MP]: Waking up workers to do linearization.");
#endif //DEBUG_PRINT_MP
	workerTask_ = LINEARIZE_DYNAMICS;
	workerWakeUpCondition_.notify_all();

#ifdef DEBUG_PRINT_MP
	printString("[MP]: Will sleep now until we have linearized dynamics.");
#endif //DEBUG_PRINT_MP

	std::unique_lock<std::mutex> waitLock(kCompletedMutex_);
	kCompletedCondition_.wait(waitLock, [this]{return kCompleted_.load() > KMax_ - KMin_;});
	waitLock.unlock();
	workerTask_ = IDLE;
#ifdef DEBUG_PRINT_MP
	printString("[MP]: Woke up again, should have linearized dynamics now.");
#endif //DEBUG_PRINT_MP


	Eigen::setNbThreads(this->settings_.nThreadsEigen); // restore Eigen multi-threading
#ifdef DEBUG_PRINT_MP
	printString("[MP]: Restoring " + std::to_string(Eigen::nbThreads()) + " Eigen threads.");
#endif //DEBUG_PRINT_MP
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::computeLinearizedDynamicsWorker(size_t threadId)
{
	while(true)
	{
		size_t k = kTaken_++;

		if (k > KMax_- KMin_)
		{
			//kCompleted_++;
			if (kCompleted_.load() > KMax_-KMin_)
				kCompletedCondition_.notify_all();
			return;
		}

#ifdef DEBUG_PRINT_MP
		if ((k+1)%100 == 0)
			printString("[Thread " + std::to_string(threadId) + "]: Linearizing for index k " + std::to_string ( KMax_-k ));
#endif

		this->computeLinearizedDynamics(threadId, KMax_-k); // linearize backwards

		kCompleted_++;
	}
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::computeQuadraticCostsAroundTrajectory(size_t firstIndex, size_t lastIndex)
{
	//! fill terminal cost
	this->initializeCostToGo();


	/*!
	 * In special cases, this function may be called for a single index, e.g. for the unconstrained GNMS real-time iteration scheme.
	 * Then, don't wake up workers, but do single-threaded computation for that single index, and return.
	 */
	if(lastIndex == firstIndex)
	{
#ifdef DEBUG_PRINT_MP
		printString("[MP]: do single threaded cost approximation for single index " + std::to_string(firstIndex) + ". Not waking up workers.");
#endif //DEBUG_PRINT_MP
		this->computeQuadraticCosts(this->settings_.nThreads, firstIndex);
		return;
	}


	/*!
	 * In case of multiple points to be linearized, start multi-threading:
	 */
	Eigen::setNbThreads(1); // disable Eigen multi-threading
#ifdef DEBUG_PRINT_MP
	printString("[MP]: Restricting Eigen to " + std::to_string(Eigen::nbThreads()) + " threads.");
#endif //DEBUG_PRINT_MP

	kTaken_ = 0;
	kCompleted_ = 0;
	KMax_ = lastIndex;
	KMin_ = firstIndex;


#ifdef DEBUG_PRINT_MP
	std::cout<<"[MP]: Waking up workers to do cost computation."<<std::endl;
#endif //DEBUG_PRINT_MP
	workerTask_ = COMPUTE_COST;
	workerWakeUpCondition_.notify_all();

#ifdef DEBUG_PRINT_MP
	std::cout<<"[MP]: Will sleep now until we have cost."<<std::endl;
#endif //DEBUG_PRINT_MP

	std::unique_lock<std::mutex> waitLock(kCompletedMutex_);
	kCompletedCondition_.wait(waitLock, [this]{return kCompleted_.load() > KMax_ - KMin_;});
	waitLock.unlock();
	workerTask_ = IDLE;
#ifdef DEBUG_PRINT_MP
	std::cout<<"[MP]: Woke up again, should have cost now."<<std::endl;
#endif //DEBUG_PRINT_MP

	Eigen::setNbThreads(this->settings_.nThreadsEigen); // restore Eigen multi-threading
#ifdef DEBUG_PRINT_MP
	printString("[MP]: Restoring " + std::to_string(Eigen::nbThreads()) + " Eigen threads.");
#endif //DEBUG_PRINT_MP
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::computeQuadraticCostsWorker(size_t threadId)
{
	while(true)
	{
		size_t k = kTaken_++;

		if (k > KMax_ - KMin_)
		{
			//kCompleted_++;
			if (kCompleted_.load() > KMax_ - KMin_)
				kCompletedCondition_.notify_all();
			return;
		}

#ifdef DEBUG_PRINT_MP
		if ((k+1)%100 == 0)
			printString("[Thread "+std::to_string(threadId)+"]: Quadratizing cost for index k "+std::to_string(KMax_ - k ));
#endif

		this->computeQuadraticCosts(threadId, KMax_ - k); // compute cost backwards

		kCompleted_++;
	}
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::computeLQProblemWorker(size_t threadId)
{
	while(true)
	{
		size_t k = kTaken_++;

		if (k > KMax_ - KMin_)
		{
			//kCompleted_++;
			if (kCompleted_.load() > KMax_ - KMin_)
				kCompletedCondition_.notify_all();
			return;
		}

#ifdef DEBUG_PRINT_MP
		if ((k+1)%100 == 0)
			printString("[Thread " + std::to_string(threadId) + "]: Building LQ problem for index k " + std::to_string(KMax_ - k));
#endif

		this->computeQuadraticCosts(threadId, KMax_-k); // compute cost backwards
		this->computeLinearizedDynamics(threadId, KMax_-k); // linearize backwards

		kCompleted_++;
	}
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::rolloutShots(size_t firstIndex, size_t lastIndex)
{
	/*!
	 * In special cases, this function may be called for a single index, e.g. for the unconstrained GNMS real-time iteration scheme.
	 * Then, don't wake up workers, but do single-threaded computation for that single index, and return.
	 */
	if(lastIndex == firstIndex)
	{
#ifdef DEBUG_PRINT_MP
		printString("[MP]: do single threaded shot rollout for single index " + std::to_string(firstIndex) + ". Not waking up workers.");
#endif //DEBUG_PRINT_MP
		this->rolloutSingleShot(this->settings_.nThreads, firstIndex);
		this->computeSingleDefect(this->settings_.nThreads, firstIndex);
		return;
	}


	/*!
	 * In case of multiple points to be linearized, start multi-threading:
	 */
	Eigen::setNbThreads(1); // disable Eigen multi-threading
#ifdef DEBUG_PRINT_MP
	printString("[MP]: Restricting Eigen to " + std::to_string (Eigen::nbThreads())  + " threads.");
#endif //DEBUG_PRINT_MP


	kTaken_ = 0;
	kCompleted_ = 0;
	KMax_ = lastIndex;
	KMin_ = firstIndex;


#ifdef DEBUG_PRINT_MP
	std::cout<<"[MP]: Waking up workers to do shot rollouts."<<std::endl;
#endif //DEBUG_PRINT_MP
	workerTask_ = ROLLOUT_SHOTS;
	workerWakeUpCondition_.notify_all();

#ifdef DEBUG_PRINT_MP
	std::cout<<"[MP]: Will sleep now until we have rolled out all shots."<<std::endl;
#endif //DEBUG_PRINT_MP

	std::unique_lock<std::mutex> waitLock(kCompletedMutex_);
	kCompletedCondition_.wait(waitLock, [this]{return kCompleted_.load() > KMax_ - KMin_;});
	waitLock.unlock();
	workerTask_ = IDLE;
#ifdef DEBUG_PRINT_MP
	std::cout<<"[MP]: Woke up again, should have rolled out all shots now."<<std::endl;
#endif //DEBUG_PRINT_MP

	Eigen::setNbThreads(this->settings_.nThreadsEigen); // restore Eigen multi-threading
#ifdef DEBUG_PRINT_MP
	printString("[MP]: Restoring " + std::to_string(Eigen::nbThreads()) + " Eigen threads.");
#endif //DEBUG_PRINT_MP
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>:: rolloutShotWorker(size_t threadId)
{
	while(true)
	{
		size_t k = kTaken_++;

		if (k > KMax_ - KMin_)
		{
			if (kCompleted_.load() > KMax_ - KMin_)
				kCompletedCondition_.notify_all();
			return;
		}

#ifdef DEBUG_PRINT_MP
		if ((k+1)%100 == 0)
			printString("[Thread " + std::to_string(threadId) + "]: rolling out shot with index " + std::to_string(KMax_ - k));
#endif

		this->rolloutSingleShot(threadId, KMax_ - k);
		this->computeSingleDefect(threadId, KMax_ - k);

		kCompleted_++;
	}
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::updateSolutionState()
{
	// todo: this is the same as in ST mode. sum them together

	this->x_ = this->lqocSolver_->getSolutionState();

	//! get state update norm. may be overwritten later, depending on the algorithm
	this->lx_norm_ = this->lqocSolver_->getStateUpdateNorm();
}

template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::updateSolutionFeedforward()
{
	this->u_ff_prev_ = this->u_ff_; // store previous feedforward for line-search

	this->u_ff_ = this->lqocSolver_->getSolutionControl();
	u_ff_fullstep_ = this->u_ff_;

	//! get control update norm. may be overwritten later, depending on the algorithm
	this->lu_norm_ = this->lqocSolver_->getControlUpdateNorm();}

template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::updateSolutionFeedback()
{
	// todo: this is the same as in ST mode. sum them together

	if(this->settings_.closedLoopShooting)
		this->L_ = this->lqocSolver_->getFeedback();
	else
		this->L_.setConstant(core::FeedbackMatrix<STATE_DIM, CONTROL_DIM, SCALAR>::Zero());
}


template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
SCALAR NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::performLineSearch()
{
	Eigen::setNbThreads(1); // disable Eigen multi-threading

	alphaProcessed_.clear();
	alphaTaken_ = 0;
	alphaBestFound_ = false;
	alphaExpBest_ = this->settings_.lineSearchSettings.maxIterations;
	alphaExpMax_ = this->settings_.lineSearchSettings.maxIterations;
	alphaProcessed_.resize(this->settings_.lineSearchSettings.maxIterations, 0);

#ifdef DEBUG_PRINT_MP
	std::cout<<"[MP]: Waking up workers."<<std::endl;
#endif //DEBUG_PRINT_MP
	workerTask_ = LINE_SEARCH;
	workerWakeUpCondition_.notify_all();

#ifdef DEBUG_PRINT_MP
	std::cout<<"[MP]: Will sleep now until we have results."<<std::endl;
#endif //DEBUG_PRINT_MP
	std::unique_lock<std::mutex> waitLock(alphaBestFoundMutex_);
	alphaBestFoundCondition_.wait(waitLock, [this]{return alphaBestFound_.load();});
	waitLock.unlock();
	workerTask_ = IDLE;
#ifdef DEBUG_PRINT_MP
	std::cout<<"[MP]: Woke up again, should have results now."<<std::endl;
#endif //DEBUG_PRINT_MP

	double alphaBest = 0.0;
	if (alphaExpBest_ != alphaExpMax_)
	{
		alphaBest = this->settings_.lineSearchSettings.alpha_0 * std::pow(this->settings_.lineSearchSettings.n_alpha, alphaExpBest_);
	}

	return alphaBest;

	Eigen::setNbThreads(this->settings_.nThreadsEigen); // restore Eigen multi-threading
#ifdef DEBUG_PRINT_MP
	printString("[MP]: Restoring " + std::to_string(Eigen::nbThreads()) + " Eigen threads.");
#endif //DEBUG_PRINT_MP

} // end linesearch



template <size_t STATE_DIM, size_t CONTROL_DIM, size_t P_DIM, size_t V_DIM, typename SCALAR>
void NLOCBackendMP<STATE_DIM, CONTROL_DIM, P_DIM, V_DIM, SCALAR>::lineSearchWorker(size_t threadId)
{
	while(true)
	{
		size_t alphaExp = alphaTaken_++;

#ifdef DEBUG_PRINT_MP
		printString("[Thread "+ std::to_string(threadId)+"]: Taking alpha index " + std::to_string(alphaExp));
#endif

		if (alphaExp >= alphaExpMax_ || alphaBestFound_)
		{
			return;
		}

		// convert to real alpha
		double alpha = this->settings_.lineSearchSettings.alpha_0 * std::pow(this->settings_.lineSearchSettings.n_alpha, alphaExp);
		typename Base::StateVectorArray x_local(1);
		typename Base::ControlVectorArray u_recorded;
		typename Base::TimeArray t_local;

		x_local[0] = this->x_[0];

		SCALAR intermediateCost;
		SCALAR finalCost;

		this->lineSearchSingleController(threadId, alpha, u_ff_fullstep_, x_local, u_recorded, t_local, intermediateCost, finalCost, &alphaBestFound_);

		SCALAR cost = intermediateCost + finalCost;

		lineSearchResultMutex_.lock();
		if (cost < this->lowestCost_)
		{
			// make sure we do not alter an existing result
			if (alphaBestFound_)
			{
				lineSearchResultMutex_.unlock();
				break;
			}

#ifdef DEBUG_PRINT_LINESEARCH
			printString("[LineSearch, Thread " + std::to_string(threadId) + "]: Lower cost found: " + std::to_string(cost) + " at alpha: " + std::to_string(alpha));
#endif //DEBUG_PRINT_LINESEARCH

#if defined (MATLAB_FULL_LOG) || defined (DEBUG_PRINT)
			this->computeControlUpdateNorm(u_recorded, this->u_ff_prev_);
			this->computeStateUpdateNorm(x_local, this->x_prev_);
#endif

			alphaExpBest_ = alphaExp;
			this->intermediateCostBest_ = intermediateCost;
			this->finalCostBest_ = finalCost;
			this->lowestCost_ = cost;
			this->x_prev_ = x_local;
			this->x_.swap(x_local);
			this->u_ff_.swap(u_recorded);
			this->t_.swap(t_local);
		} else
		{
#ifdef DEBUG_PRINT_LINESEARCH
			printString("[LineSearch, Thread " + std::to_string(threadId) + "]: No lower cost found, cost " + std::to_string(cost) +" at alpha "
					+ std::to_string(alpha)+" . Best cost was " + std::to_string(this->lowestCost_));
#endif //DEBUG_PRINT_LINESEARCH
		}

		alphaProcessed_[alphaExp] = 1;

		// we now check if all alphas prior to the best have been processed
		// this also covers the case that there is no better alpha
		bool allPreviousAlphasProcessed = true;
		for (size_t i=0; i<alphaExpBest_; i++)
		{
			if (alphaProcessed_[i] != 1)
			{
				allPreviousAlphasProcessed = false;
				break;
			}
		}
		if (allPreviousAlphasProcessed)
		{
			alphaBestFound_ = true;
			alphaBestFoundCondition_.notify_all();
		}

		lineSearchResultMutex_.unlock();
	}
}

} // namespace optcon
} // namespace ct

