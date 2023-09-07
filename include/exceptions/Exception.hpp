/**
 * @file Exception.hpp
 * @author machinetherapist@gmail.com (MBK)
 * @brief Contains the implementation of an exception that all other exceptions inherit from.
 * @version 0.1
 * @date 2023-09-07
 */

#include <exception>

namespace sockcanpp { namespace exceptions {

    using std::exception;

    /**
     * @brief An exception that all other exceptions inherit from.
     */
    class Exception: public exception {
    };

} /* exceptions */ } /* sockcanpp */
