#ifndef CT_OPTCON_OPTVECTOR_HPP_
#define CT_OPTCON_OPTVECTOR_HPP_

namespace ct {
namespace optcon {

class OptVector
{
public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
	typedef double Number;
	typedef Eigen::Matrix<Number, Eigen::Dynamic, 1> VectorXd;
	typedef Eigen::Matrix<int, Eigen::Dynamic, 1> VectorXi;
	typedef Eigen::Map<VectorXd> MapVecXd;
	typedef Eigen::Map<VectorXi> MapVecXi;
	typedef Eigen::Map<const VectorXd> MapConstVecXd;

	OptVector() = delete;

	OptVector(size_t n) :
	newXCounter_(0)
	{
		xInitGuess_.resize(n);
		x_.resize(n);
		x_.setZero();
		xLb_.resize(n);
		xUb_.resize(n);
		xLb_.setConstant(std::numeric_limits<double>::lowest());
		xUb_.setConstant(std::numeric_limits<double>::max());
		xMul_.resize(n);
		xMul_.setZero();
		xState_.resize(n);
		xState_.setZero();
		zuInitGuess_.resize(n);
		zuInitGuess_.setZero();
		zlInitGuess_.resize(n);
		zlInitGuess_.setZero();
	}

	void resizeConstraintVars(size_t m)
	{
		zMul_.resize(m+1);
		zMul_.setZero();
		zState_.resize(m+1);
		zState_.setZero();
	}

	~OptVector(){}

	void resize(const size_t size) {
		x_.resize(size);
		xLb_.resize(size);
		xUb_.resize(size);
		xInitGuess_.resize(size);
		boundLowerMultGuess_.resize(size);
		boundUpperMultGuess_.resize(size);
		lambdaGuess_.resize(size);
		zuInitGuess_.resize(size);
		zlInitGuess_.resize(size);
	}

	void setZero() {
		x_.setZero();
		xLb_.setZero();
		xUb_.setZero();
		xInitGuess_.setZero();
		boundLowerMultGuess_.setZero();
		boundUpperMultGuess_.setZero();
		lambdaGuess_.setZero();
		zuInitGuess_.setZero();
		zlInitGuess_.setZero();
	}

	bool checkPrimalVarDimension(unsigned int n)
	{
		bool xDim = x_.size() == n ? true : false;
		bool xLDim = xLb_.size() == n ? true : false;
		bool xUDim = xUb_.size() == n ? true : false;
		return xDim && xLDim && xUDim;
	}

	void setBounds(const Eigen::VectorXd& xLb, const Eigen::VectorXd& xUb){
		xLb_ = xLb;
		xUb_ = xUb;
	}

	Eigen::VectorXd get() const {
		return x_;
	}

	void getLowerBounds(MapVecXd& x) const {
		x = xLb_;
	}

	void getUpperBounds(MapVecXd& x) const {
		x = xUb_;
	}

	void getPrimalMultState(const size_t n, MapVecXd& xMul, Eigen::Map<Eigen::VectorXi>& xState) const {
		assert(n == xMul_.size());
		assert(n == xState_.size());
		xMul = xMul_;
		xState = xState_;
	}

	void getDualMultState(const size_t m, MapVecXd& zMul, Eigen::Map<Eigen::VectorXi>& zState) const {
		assert(m == zMul_.size());
		assert(m == zState_.size());
		zMul = zMul_;
		zState = zState_;
	}

	size_t size() const {return x_.size();}

	void getInitPrimalVars(size_t n, MapVecXd& x)  const {
		assert (n == xInitGuess_.size());
		x = xInitGuess_;
	}

	void getPrimalVars(size_t n, MapVecXd& x)  const {
		assert (n == xInitGuess_.size());
		x = x_;
	}

	void getInitBoundMultipliers(size_t n, MapVecXd& low, MapVecXd& up) const {
		assert(n == boundLowerMultGuess_.size());
		low = boundLowerMultGuess_;
		up = boundUpperMultGuess_;
	}
	void getInitLambdaVars(size_t m, MapVecXd& x) const {
		assert(m == lambdaGuess_.size());
		x = lambdaGuess_;
	}

	void setNewSolution(
		const MapConstVecXd& x, 
		const MapConstVecXd& zL, 
		const MapConstVecXd& zU, 
		const MapConstVecXd& lambda)
	{
		x_ = x;
		zlInitGuess_ = zL;
		zuInitGuess_ = zU;
		lambdaGuess_ = lambda;
	}

	void setNewSolution(const MapVecXd& x, const MapVecXd& xMul, 
		const MapVecXi& xState, const MapVecXd& fMul, const MapVecXi& fState)
	{
		x_ = x;
		xMul_ = xMul;
		xState_ = xState;
		zMul_ = fMul;
		zState_ = fState;
	}

	void setPrimalVars(const MapConstVecXd& x, bool isNew)
	{
		if(isNew)
		{
			x_ = x;
			newXCounter_++;
		}		
	}

	size_t getCounter() const{
		return newXCounter_;
	}

	void setInitGuess(){
		x_ = xInitGuess_;
	}

	Eigen::VectorXd getInitGuess() const{
		return xInitGuess_;
	}

	Eigen::VectorXd getPrimalVars() const{
		return x_;
	}

	size_t getNewXCounter() const{
		return newXCounter_;
	}


protected:
	Eigen::VectorXd x_; 	//Primal vars
	Eigen::VectorXd xLb_;	/*!< lower bound on optimization vector */
	Eigen::VectorXd xUb_;	/*!< upper bound on optimization vector */
	Eigen::VectorXd xInitGuess_;
	Eigen::VectorXd boundLowerMultGuess_;
	Eigen::VectorXd boundUpperMultGuess_;
	Eigen::VectorXd zuInitGuess_;
	Eigen::VectorXd zlInitGuess_;

	// Snopt stuff
	Eigen::VectorXd lambdaGuess_;
	Eigen::VectorXd xMul_;
	Eigen::VectorXi xState_;
	Eigen::VectorXd	zMul_;
	Eigen::VectorXi	zState_;

	size_t newXCounter_;

};

}
}


#endif