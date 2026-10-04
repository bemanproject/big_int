// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

#include <beman/big_int/big_int.hpp>

#include <iomanip>
#include <iostream>
#include <span>
#include <string>

using beman::big_int::big_int;

auto sqrt(big_int m) -> big_int {
    // Calculate the square root.

    big_int s{};

    if (m <= 0) {
        s = 0;
    } else {
        // Obtain the initial guess via algorithms
        // involving the position of the msb.
        const std::size_t msb_pos{m.size() - 1U};

        const auto msb_pos_mod_2 = msb_pos % 2;

        // Obtain the initial value.
        const auto left_shift_amount = 1 + ((msb_pos_mod_2 == 0) ? msb_pos / 2 : (msb_pos + 1) / 2);

        big_int u{big_int{1} << left_shift_amount};

        // Perform the iteration for the square root.
        // See Algorithm 1.13 SqrtInt, Sect. 1.5.1
        // in R.P. Brent and Paul Zimmermann, "Modern Computer Arithmetic",
        // Cambridge University Press, 2011.

        for (auto i{0}; i < 64; ++i) {
            static_cast<void>(i);

            s = u;

            u = (s + (m / s)) / 2;

            if (u >= s) {
                break;
            }
        }
    }

    return s;
}

auto cbrt(big_int m) -> big_int {
    // Calculate the cube root.

    big_int s{};

    if (m < 0) {
        s = -cbrt(-m);
    } else if (m == 0) {
        s = 0;
    } else {
        // Obtain the initial guess via algorithms
        // involving the position of the msb.
        const std::size_t msb_pos{m.size() - 1U};

        // Obtain the initial value.
        const auto msb_pos_mod_3 = msb_pos % 3;

        const auto left_shift_amount =
            1 + ((msb_pos_mod_3 == 0) ? 1 + msb_pos / 3 : (msb_pos + (3 - msb_pos_mod_3)) / 3);

        big_int u{big_int{1} << left_shift_amount};

        // Perform the iteration for the k'th root (applied for k = 3).
        // See Algorithm 1.14 RootInt, Sect. 1.5.2
        // in R.P. Brent and Paul Zimmermann, "Modern Computer Arithmetic",
        // Cambridge University Press, 2011.

        for (auto i{0}; i < 64; ++i) {
            static_cast<void>(i);

            s = u;

            u = ((s * 2) + (m / (s * s))) / 3;

            if (u >= s) {
                break;
            }
        }
    }

    return s;
}

auto main() -> int {
    using namespace beman::big_int::literals;

    const big_int arg_x{
        31415926535897932384626433832795028841971693993751058209749445923078164062862089986280348253421170679821480865132823066470938446095505822317253594081284811174502841027019385211055596446229489549303819644288109756659334461284756482337867831652712019091456485669234603486104543266482133936072602491412737245870066063155881748815209209628292540917153643678925903600113305305488204665213841469519415116094330572703657595919530921861173819326117931051185480744623799627495673518857527248912279381830119491298336733624406566430860213949463952247371907021798609437027705392171762931767523846748184676694051320005681271452635608277857713427577896091736371787214684409012249534301465495853710507922796892589235420199561121290219608640344181598136297747713099605187072113499999983729780499510597317328160963185950244594553469083026425223082533446850352619311881710100031378387528865875332083814206171776691473035982534904287554687311595628638823537875937519577818577805321712268066130019278766111959092164201989_n};

    const big_int ctrl_sqrt{
        177245385090551602729816748334114518279754945612238712821380778985291128459103218137495065673854466541622682362428257066623615286572442260252509370960278706846203769865310512284992517302895082622893209537926796280017463901535147972051670019018523401858544697449491264031392177552590621640541933250090639840761373347747515343366798978936585183640879545116516173876005906739343179133280985484624818490205465485219561325156164746751504273876105610799612710721006037204448367236529661370809432349883166842_n};

    const big_int ctrl_cbrt{
        3155367569301821867326519405336421207498251961314999997901193388809739079012897744887631254739292007907947433618484758481627548989501719089220482459948775432816342410555282540612960501433021296640960423450227920209938211887719077129428385543361947758023983184697480787087966223432486682721792221472648796720202642738747343051078071241_n};

    const big_int result_sqrt{sqrt(arg_x)};
    const big_int result_cbrt{cbrt(arg_x)};

    const bool result_is_ok{(result_sqrt == ctrl_sqrt) && (result_cbrt == ctrl_cbrt)};

    std::cout << "result_sqrt:\n" << to_string(result_sqrt) << std::endl;
    std::cout << "\nresult_cbrt:\n" << to_string(result_cbrt) << std::endl;

    std::cout << "\nresult_is_ok: " << std::boolalpha << result_is_ok << std::endl;

    return result_is_ok ? 0 : -1;
}
