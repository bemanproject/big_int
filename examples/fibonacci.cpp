// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int.hpp>

#include <iomanip>
#include <iostream>

template <class BigIntType>
struct Matrix2x2 {
    BigIntType a{}, b{}, c{}, d{};
};

template <class BigIntType>
Matrix2x2<BigIntType> multiply(const Matrix2x2<BigIntType>& x, const Matrix2x2<BigIntType>& y) {
    // 2x2 matrix multiplication.

    return {x.a * y.a + x.b * y.c, x.a * y.b + x.b * y.d, x.c * y.a + x.d * y.c, x.c * y.b + x.d * y.d};
}

template <class BigIntType>
Matrix2x2<BigIntType> power(Matrix2x2<BigIntType> base, unsigned n) {
    // Implement fast exponentiation by squaring.

    // Start with the identity matrix.
    Matrix2x2<BigIntType> result{BigIntType{1}, BigIntType{0}, BigIntType{0}, BigIntType{1}};

    while (n > 0U) {
        if ((n & 1U) != 0U) {
            result = multiply(result, base);
        }

        base = multiply(base, base);

        n >>= 1;
    }

    return result;
}

template <class BigIntType>
BigIntType fibonacci(unsigned n) {
    // Calculate the n'th Fibonacci number using matrix exponentiation.

    if (n == 0U) {
        return BigIntType{0};
    }

    Matrix2x2<BigIntType> fibMatrix{BigIntType{1}, BigIntType{1}, BigIntType{1}, BigIntType{0}};

    Matrix2x2<BigIntType> result = power(fibMatrix, n - 1U);

    return result.a;
}

auto main() -> int {
    using beman::big_int::big_int;

    // Compute the 10,000th Fibonacci number.
    const big_int fib_10000{fibonacci<big_int>(10000U)};

    using namespace beman::big_int::literals;

    // The result is very long. Literals may exceed compiler limits
    // for some implementations. So we work with a control string here.

    const std::string str_ctrl{"3364476487643178326662161200510754331030214846068006390656476997"
                               "4680081442166662368155595513633734025582065332680836159373734790"
                               "4838652682630408924630564318873545443695598274916066020998841839"
                               "3386465273130008883026923567361313511757929743785441375213052050"
                               "4347701602264758318906527890855154366159582987279682987510631200"
                               "5754287834532155151038708182989697916131278562650331954871402142"
                               "8753269818796204693609787990035096230229102636813149319527563022"
                               "7837628441540360584402572114334961180023091208287046088923962328"
                               "8354615057765832712525460935911282039252853934346209042452489294"
                               "0390170623388899108584106518317336043747073790855263176432573399"
                               "3712871937587746897479926305837065742830161637408969178426378624"
                               "2128352581128205163702980893320999057079200643674262023897831114"
                               "7005407499845925036063356093388383192338678305613643535189213327"
                               "9732908133732642652633989763922723407882928177953580570993691049"
                               "1754708089318410561463223382174656373212482263830921032977016480"
                               "5472624384237486241145309381220656491403275108664339451751216152"
                               "6545361333111314042436854805106765843493523836959653428071768775"
                               "3283482343455573667197313927462736291082106792807847180353291311"
                               "7677892465908993863545932789452377767440619224033763867400402133"
                               "0343297496902028328145933418826817683893072003634795623117103101"
                               "2919531697946076327375892535307725523759437884345040677155557790"
                               "5645044301664011946258097221672975861502696844314695203461493229"
                               "1105970676243268515992834709891284706740862008587135016260312071"
                               "9031720860940812983215810772820763531866246112782455372085323653"
                               "0577595643007251774431505153960090516860322034916322264088524885"
                               "2433158051534849622434848299380905070483482449327453732624567755"
                               "8790891871908036620580095947431500524025327097469953187707243768"
                               "2590741993963226598414749819360928522394503970716544315642132815"
                               "7688908058783183404917434556270520223564846495196112460268313970"
                               "9750693826487066132645076650746115126775227486215986425307112984"
                               "4118262266105716351506926002986170494542504749137811515413994155"
                               "0671256271197133252763631939606902895650288268608362241082050562"
                               "430701794976171121233066073310059947366875"};

    // The to_string() function gives us a very convenient way to verify
    // the numerical correctness.

    const std::string str_fib_10000{to_string(fib_10000)};

    const bool result_is_ok{str_fib_10000 == str_ctrl};

    std::cout << "fib_10000:\n"
              << to_string(fib_10000) << "\n\nresult_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
