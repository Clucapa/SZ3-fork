//
// Created by Xin Liang on 03/09/2022.
//

#ifndef SZ_QOI_LOG_X_HPP
#define SZ_QOI_LOG_X_HPP

#include <cmath>
#include <algorithm>
#include "QoZ/def.hpp"
#include "QoZ/qoi/QoI.hpp"
#include "QoZ/utils/Iterator.hpp"

namespace QoZ {
    template<class T, uint N>
    class QoI_Log_X : public concepts::QoIInterface<T, N> {

    public:
        QoI_Log_X(double tolerance, T global_eb, double base = 2.0) : 
                tolerance(tolerance),
                global_eb(global_eb),
                base(base) {
            // TODO: adjust type for int data
            //printf("global_eb = %.4f\n", (double) global_eb);
            //printf("tolerance = %.4f\n", (double) tolerance);
            // assuming b > 1
            //T coeff1 = fabs(1 - pow(base, - tolerance));
            ///T coeff2 = fabs(pow(base, tolerance) - 1);
           /// coeff = std::min(coeff1, coeff2);
            ///printf("coeff1 = %.4f, coeff2 = %.4f\n", (double) coeff1, (double) coeff2);
            ///printf("coeff = %.4f\n", (double) coeff);
            log_b = log(base);
            //printf("log base = %.4f\n", log_b);
            concepts::QoIInterface<T, N>::id = 2;
        }

        using Range = multi_dimensional_range<T, N>;
        using iterator = typename multi_dimensional_range<T, N>::iterator;

        T interpret_eb(T data) const {
            // if b > 1
            // e = min{(1 - b^{-t})|x|, (b^{t} - 1)|x|}
            // return 0;
            //if(data == 0) return global_eb;
            //double a = fabs(1.0 / (data*log_b) );//datatype may be T
            //double b = fabs(-a/data);
            //T eb = std::min((sqrt(a*a+2*b*tolerance)-a)/b,fabs(data));
            if (data == 0)
                return 0;
            data = fabs(data);

            T eb = (1-pow(base,-tolerance))*data;
            //T eb = coeff * fabs(data);
            return std::min(eb, global_eb);
        }

        T interpret_eb(const iterator &iter) const {
            return interpret_eb(*iter);
        }

        T interpret_eb(const T * data, ptrdiff_t offset) {
            return interpret_eb(*data);
        }

        bool check_compliance(T data, T dec_data, bool verbose=false) const {
            if (data == 0) return (dec_data == 0);
            if (dec_data == 0) return false;

            return (fabs( log_b_a(fabs(dec_data)) - log_b_a(fabs(data)) ) < tolerance );
        }

        void update_tolerance(T data, T dec_data){}

        void precompress_block(const std::shared_ptr<Range> &range){}

        void postcompress_block(){}

        void print(){}

        T get_global_eb() const { return global_eb; }

        void set_global_eb(T eb) {global_eb = eb;}

        void init(){}

        void set_dims(const std::vector<size_t>& new_dims){}

        double eval(T val) const{
            
            
            return log_b_a(fabs(val)); 

        } 

        std::string get_expression(const std::string var="x") const{
            return "ln(|"+var+"|)/"+std::to_string(log_b);
        }

        void pre_compute(const T * data){}

        void set_qoi_tolerance(double tol) {tolerance = tol;}

    private:
        inline T log_b_a(T a) const {
            return log(a) / log_b;
        }
        double tolerance;
        T global_eb;
        double coeff;
        double log_b;
        double base;
    };
}

namespace QoZ {
    template<class T, uint N>
    class QoI_Log_X_Approx : public concepts::QoIInterface<T, N> {

    public:
        QoI_Log_X_Approx(double tolerance, T global_eb, double base = 2.0) : 
                tolerance(tolerance),
                global_eb(global_eb),
                base(base) {
            // TODO: adjust type for int data
            //printf("global_eb = %.4f\n", (double) global_eb);
            //printf("tolerance = %.4f\n", (double) tolerance);
            // assuming b > 1
            //T coeff1 = fabs(1 - pow(base, - tolerance));
            ///T coeff2 = fabs(pow(base, tolerance) - 1);
           /// coeff = std::min(coeff1, coeff2);
            ///printf("coeff1 = %.4f, coeff2 = %.4f\n", (double) coeff1, (double) coeff2);
            ///printf("coeff = %.4f\n", (double) coeff);
            log_b = log(base);
            //printf("log base = %.4f\n", log_b);
            concepts::QoIInterface<T, N>::id = 2;
        }

        using Range = multi_dimensional_range<T, N>;
        using iterator = typename multi_dimensional_range<T, N>::iterator;

        T interpret_eb(T data) const {
            // if b > 1
            // e = min{(1 - b^{-t})|x|, (b^{t} - 1)|x|}
            // return 0;
            data = fabs(data);
            if(data == 0) return global_eb;
            double a = fabs(1.0 / (data*log_b) );//datatype may be T
            double b = fabs(-a/data);
            T eb;

            if(!std::isnan(a) and !std::isnan(b) and !std::isinf(a) and !std::isinf(b)and b >= 1e-10 )
                eb = (sqrt(a*a+2*b*tolerance)-a)/b;
            else if (!std::isnan(a) and !std::isinf(a) and a!=0 )
                eb = tolerance/a;
            else 
                eb = global_eb;
            eb = std::min(eb,data);
            //if (data == 0)
            //    return 0;
            //data = fabs(data);

            //T eb = (1-pow(base,-tolerance))*data;
            //T eb = coeff * fabs(data);
            return std::min(eb, global_eb);
        }

        T interpret_eb(const iterator &iter) const {
            return interpret_eb(*iter);
        }

        T interpret_eb(const T * data, ptrdiff_t offset) {
            return interpret_eb(*data);
        }

        bool check_compliance(T data, T dec_data, bool verbose=false) const {
            if (data == 0) return (dec_data == 0);
            if (dec_data == 0) return false;

            return (fabs( log_b_a(fabs(dec_data)) - log_b_a(fabs(data)) ) < tolerance );
        }

        void update_tolerance(T data, T dec_data){}

        void precompress_block(const std::shared_ptr<Range> &range){}

        void postcompress_block(){}

        void print(){}

        T get_global_eb() const { return global_eb; }

        void set_global_eb(T eb) {global_eb = eb;}

        void init(){}

        void set_dims(const std::vector<size_t>& new_dims){}

        double eval(T val) const{
            
            
            return log_b_a(fabs(val)); 

        } 

        std::string get_expression(const std::string var="x") const{
            return "ln(|"+var+"|)/"+std::to_string(log_b);
        }

        void pre_compute(const T * data){}

        void set_qoi_tolerance(double tol) {tolerance = tol;}

    private:
        inline T log_b_a(T a) const {
            return log(a) / log_b;
        }
        double tolerance;
        T global_eb;
        //double coeff;
        double log_b;
        double base;
    };
}


#endif 