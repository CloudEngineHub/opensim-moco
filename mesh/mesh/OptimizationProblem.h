#ifndef MESH_OPTIMIZATIONPROBLEM_H
#define MESH_OPTIMIZATIONPROBLEM_H

#include "common.h"
#include <memory>

namespace mesh {

class OptimizationProblemProxy {
    // TODO rename all method names to verbs
public:
    // TODO might not use eigen...just raw C arrays...
    virtual unsigned num_variables() const = 0;
    virtual unsigned num_constraints() const = 0;
    virtual const Eigen::VectorXd& variable_lower_bounds() const = 0;
    virtual const Eigen::VectorXd& variable_upper_bounds() const = 0;
    virtual const Eigen::VectorXd& constraint_lower_bounds() const = 0;
    virtual const Eigen::VectorXd& constraint_upper_bounds() const = 0;
    /// Create an initial guess for this problem according to the
    /// following rules:
    ///   - unconstrained variable: 0.
    ///   - lower and upper bounds: midpoint of the bounds.
    ///   - only one bound: value of the bound.
    Eigen::VectorXd initial_guess_from_bounds() const;
    // TODO b/c of SNOPT, want to be able to ask for sparsity separately.
    virtual void sparsity(const Eigen::VectorXd& variables,
            std::vector<unsigned int>& jacobian_row_indices,
            std::vector<unsigned int>& jacobian_col_indices,
            std::vector<unsigned int>& hessian_row_indices,
            std::vector<unsigned int>& hessian_col_indices) const = 0;
    // TODO provide alternatives that take raw buffers. This is the interface
    // for optimizers, after all...
    virtual void objective(unsigned num_variables, const double* variables,
            bool new_variables,
            double& obj_value) const = 0;
    virtual void constraints(unsigned num_variables, const double* variables,
            bool new_variables,
            unsigned num_constraints, double* constr) const = 0;
    virtual void gradient(unsigned num_variables, const double* variables,
            bool new_variables,
            double* grad) const = 0;
    virtual void jacobian(unsigned num_variables, const double* variables,
            bool new_variables,
            unsigned num_nonzeros, double* nonzeros) const = 0;
    // TODO this signature seems to be tailoring to Ipopt too much.
    virtual void hessian_lagrangian(
            unsigned num_variables, const double* variables,
            bool new_variables, double obj_factor,
            unsigned num_constraints, const double* lambda,
            bool new_lambda,
            unsigned num_nonzeros, double* nonzeros) const = 0;
    // TODO consider providing alternative signatures like:
    // void objective(const Eigen::VectorXd& variables,
    //         bool new_variables = true,
    //         double& obj_value) const = 0;
};

class AbstractOptimizationProblem {
public:
    virtual std::shared_ptr<OptimizationProblemProxy> make_proxy() const = 0;
};

template<typename T>
class OptimizationProblem : public AbstractOptimizationProblem {
public:
    class Proxy;

    virtual ~OptimizationProblem() = default;

    OptimizationProblem() = default;

    OptimizationProblem(unsigned num_variables, unsigned num_constraints)
            :m_num_variables(num_variables),
             m_num_constraints(num_constraints) { }

    std::shared_ptr<OptimizationProblemProxy> make_proxy() const override final;

    // TODO can override to provide custom derivatives.
    //virtual void gradient(const std::vector<T>& x, std::vector<T>& grad) const;
    //virtual void jacobian(const std::vector<T>& x, TODO) const;
    //virtual void hessian() const;

    unsigned get_num_variables() const { return m_num_variables; }

    unsigned get_num_constraints() const { return m_num_constraints; }

    // TODO it might be weird for users to use/implement a method called "_impl"
    virtual void objective(const VectorX<T>& x, T& obj_value) const;

    virtual void constraints(const VectorX<T>& x,
            Eigen::Ref<VectorX<T>> constr) const;

    // TODO user may want to define gradient, jacobian, hessian on their own...

//    void objective(const Eigen::VectorXd& variables, double& obj_value) const;
//
//    void constraints(const Eigen::VectorXd& variables,
//            Eigen::Ref<Eigen::VectorXd> constr) const;
//    void objective(const VectorX<T>& x, T& obj_value) const;
//
//    void constraints(const VectorX<T>& x,
//            Eigen::Ref<VectorX<T>> constr) const;

    // TODO for both hessian and jacobian.
    // TODO what about multiple points used to determine sparsity?
    // TODO this would move to a proxy class.
//    void determine_sparsity(const Eigen::VectorXd& variables,
//            std::vector<unsigned int>& jacobian_row_indices,
//            std::vector<unsigned int>& jacobian_col_indices,
//            std::vector<unsigned int>& hessian_row_indices,
//            std::vector<unsigned int>& hessian_col_indices) const;

    const Eigen::VectorXd& get_variable_lower_bounds() const
    {
        return m_variable_lower_bounds;
    }

    const Eigen::VectorXd& get_variable_upper_bounds() const
    {
        return m_variable_upper_bounds;
    }

    const Eigen::VectorXd& get_constraint_lower_bounds() const
    {
        return m_constraint_lower_bounds;
    }

    const Eigen::VectorXd& get_constraint_upper_bounds() const
    {
        return m_constraint_upper_bounds;
    }
protected:

    void set_num_variables(unsigned num_variables)
    {
        // TODO if set, invalidate variable bounds.
        m_num_variables = num_variables;
    }

    void set_num_constraints(unsigned num_constraints)
    {
        m_num_constraints = num_constraints;
    }

    // TODO eigen wants these to be more generic to avoid temporaries.
    // TODO allow specifying these as std::vector<std::pair<double>>;
    // this is a more logical way to specify bounds for users.
    void set_variable_bounds(const Eigen::VectorXd& lower,
            const Eigen::VectorXd& upper)
    {
        // TODO make sure num_variables has been set.
        // TODO can only call this if m_num_variables etc are already set.
        assert(lower.size() == m_num_variables);
        assert(upper.size() == m_num_variables);
        // TODO where should this ordering check go?
        // TODO make sure that this check works (test it).
        assert((lower.array() <= upper.array()).all());
        m_variable_lower_bounds = lower;
        m_variable_upper_bounds = upper;
    }

    void set_constraint_bounds(const Eigen::VectorXd& lower,
            const Eigen::VectorXd& upper)
    {
        assert(lower.size()==m_num_constraints);
        assert(upper.size()==m_num_constraints);
        // TODO assert(lower <= upper);
        m_constraint_lower_bounds = lower;
        m_constraint_upper_bounds = upper;
    }

private:

    // TODO this feels too Ipopt-specific..obj_factor?
	// TODO can delete this now; the TODO above is outdated.
    // void lagrangian(double obj_factor, const VectorX<T>& x,
    //         const Eigen::VectorXd& lambda, T& result) const;


    // TODO use safer types that will give exceptions for improper values.
    unsigned m_num_variables;
    unsigned m_num_constraints;
    Eigen::VectorXd m_variable_lower_bounds;
    Eigen::VectorXd m_variable_upper_bounds;
    Eigen::VectorXd m_constraint_lower_bounds;
    Eigen::VectorXd m_constraint_upper_bounds;
};

template<typename T>
std::shared_ptr<OptimizationProblemProxy> OptimizationProblem<T>::make_proxy()
const
{
    // TODO is this what we want? a shared_ptr??
    return std::make_shared<Proxy>(*this);
}

// TODO rename back to "objective()"
template<typename T>
void OptimizationProblem<T>::objective(const VectorX<T>&, T&) const
{
    // TODO proper error messages.
    throw std::runtime_error("Not implemented.");
}

template<typename T>
void OptimizationProblem<T>::constraints(const VectorX<T>&,
        Eigen::Ref<VectorX<T>>) const
{
// TODO throw std::runtime_error("Not implemented.");
}


// TODO The non-specialized version is empty!
template<typename T>
class OptimizationProblem<T>::Proxy : public OptimizationProblemProxy {
};

template<>
class OptimizationProblem<adouble>::Proxy : public OptimizationProblemProxy {
public:
    Proxy(const OptimizationProblem<adouble>& problem) : m_problem(problem) {}
    ~Proxy() {
        // TODO try to use modern memory management.
        delete [] m_jacobian_row_indices;
        delete [] m_jacobian_col_indices;
        delete [] m_jacobian_values;
        m_jacobian_row_indices = nullptr;
        m_jacobian_col_indices = nullptr;
        m_jacobian_values = nullptr;
        delete [] m_hessian_row_indices;
        delete [] m_hessian_col_indices;
        delete [] m_hessian_values;
        m_hessian_row_indices = nullptr;
        m_hessian_col_indices = nullptr;
        m_hessian_values = nullptr;
    }
    // TODO might not use eigen...just raw C arrays...
    unsigned num_variables() const override
    {   return m_problem.get_num_variables(); }
    unsigned num_constraints() const override
    {   return m_problem.get_num_constraints(); }
    const Eigen::VectorXd& variable_lower_bounds() const override
    {   return m_problem.get_variable_lower_bounds(); }
    const Eigen::VectorXd& variable_upper_bounds() const override
    {   return m_problem.get_variable_upper_bounds(); }
    const Eigen::VectorXd& constraint_lower_bounds() const override
    {   return m_problem.get_constraint_lower_bounds(); }
    const Eigen::VectorXd& constraint_upper_bounds() const override
    {   return m_problem.get_constraint_upper_bounds(); }
    void sparsity(const Eigen::VectorXd& variables,
            std::vector<unsigned int>& jacobian_row_indices,
            std::vector<unsigned int>& jacobian_col_indices,
            std::vector<unsigned int>& hessian_row_indices,
            std::vector<unsigned int>& hessian_col_indices) const override;
    void objective(unsigned num_variables, const double* variables,
            bool new_variables,
            double& obj_value) const override;
    void constraints(unsigned num_variables, const double* variables,
            bool new_variables,
            unsigned num_constraints, double* constr) const override;
    void gradient(unsigned num_variables, const double* variables,
            bool new_variables,
            double* grad) const override;
    void jacobian(unsigned num_variables, const double* variables,
            bool new_variables,
            unsigned num_nonzeros, double* nonzeros) const override;
    void hessian_lagrangian(unsigned num_variables, const double* variables,
            bool new_variables, double obj_factor,
            unsigned num_constraints, const double* lambda,
            bool new_lambda,
            unsigned num_nonzeros, double* nonzeros) const override;
private:
    void trace_objective(short int tag,
            unsigned num_variables, const double* variables,
            double& obj_value) const;
    void trace_constraints(short int tag,
            unsigned num_variables, const double* variables,
            unsigned num_constraints, double* constr) const;
//    void lagrangian(double obj_factor,
//            unsigned num_variables, const adouble* variables,
//            unsigned num_constraints, const double* lambda,
//            adouble& result) const;
    void lagrangian(double obj_factor, const VectorXa& x,
            const Eigen::VectorXd& lambda, adouble& result) const;
    // TODO trace_lagrangian.


    const OptimizationProblem<adouble>& m_problem;
    static const short int m_objective_tag   = 1;
    static const short int m_constraints_tag = 2;
    // TODO don't make these mutable?
     // These arrays are allocated by ADOL-C, but we must delete them.
    mutable int m_jacobian_num_nonzeros = -1; /*TODO*/
    mutable unsigned int* m_jacobian_row_indices = NULL;
    mutable unsigned int* m_jacobian_col_indices = NULL;
    mutable double* m_jacobian_values = NULL;

    mutable int m_hessian_num_nonzeros = -1;
    mutable unsigned int* m_hessian_row_indices = NULL;
    mutable unsigned int* m_hessian_col_indices = NULL;
    mutable double* m_hessian_values = NULL;

    mutable int m_countTODO = 0;
    mutable int m_countJTODO = 0;
};


} // namespace mesh

#endif // MESH_OPTIMIZATIONPROBLEM_H
