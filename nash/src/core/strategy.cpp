#include "nash/core/strategy.hpp"
#include <cmath>
#include <iomanip>
#include <numeric>

namespace nash
{

    bool StrategyProfile::is_valid(double tol) const
    {
        auto check = [&](const Vec &s)
        {
            if (s.empty())
                return false;
            double sum = 0.0;
            for (double v : s)
            {
                if (v < -tol)
                    return false;
                sum += v;
            }
            return std::fabs(sum - 1.0) < tol;
        };
        return check(strategy_1) && check(strategy_2);
    }

    std::vector<std::size_t> StrategyProfile::support_1(double tol) const
    {
        std::vector<std::size_t> supp;
        for (std::size_t i = 0; i < strategy_1.size(); ++i)
            if (strategy_1[i] > tol)
                supp.push_back(i);
        return supp;
    }

    std::vector<std::size_t> StrategyProfile::support_2(double tol) const
    {
        std::vector<std::size_t> supp;
        for (std::size_t j = 0; j < strategy_2.size(); ++j)
            if (strategy_2[j] > tol)
                supp.push_back(j);
        return supp;
    }

    const char *to_string(SolverStatus s)
    {
        switch (s)
        {
        case SolverStatus::Converged:
            return "Converged";
        case SolverStatus::MaxIterations:
            return "MaxIterations";
        case SolverStatus::NumericalFailure:
            return "NumericalFailure";
        case SolverStatus::Infeasible:
            return "Infeasible";
        case SolverStatus::NotRun:
            return "NotRun";
        }
        return "Unknown";
    }

    void EquilibriumResult::print(std::ostream &os) const
    {
        os << "=== Equilibrium Result ===" << std::endl;
        os << "Solver: " << solver_name << std::endl;
        os << "Status: " << to_string(status) << std::endl;
        os << "Iterations: " << iterations << std::endl;
        os << std::scientific << std::setprecision(6);
        os << "Residual: " << residual << std::endl;
        os << std::fixed << std::setprecision(6);

        os << "Player 1 strategy: [";
        for (std::size_t i = 0; i < profile.strategy_1.size(); ++i)
        {
            if (i > 0)
                os << ", ";
            os << profile.strategy_1[i];
        }
        os << "]" << std::endl;

        os << "Player 2 strategy: [";
        for (std::size_t j = 0; j < profile.strategy_2.size(); ++j)
        {
            if (j > 0)
                os << ", ";
            os << profile.strategy_2[j];
        }
        os << "]" << std::endl;

        os << "Payoffs: (" << payoff_1 << ", " << payoff_2 << ")" << std::endl;
    }

    void MultiEquilibriumResult::print(std::ostream &os) const
    {
        os << "=== Multi-Equilibrium Result ===" << std::endl;
        os << "Solver: " << solver_name << std::endl;
        os << "Status: " << to_string(status) << std::endl;
        os << "Found " << equilibria.size() << " equilibri"
           << (equilibria.size() == 1 ? "um" : "a") << std::endl;
        os << "Total iterations: " << total_iterations << std::endl;
        os << std::endl;
        for (std::size_t k = 0; k < equilibria.size(); ++k)
        {
            os << "--- Equilibrium " << (k + 1) << " ---" << std::endl;
            equilibria[k].print(os);
            os << std::endl;
        }
    }

} // namespace nash
