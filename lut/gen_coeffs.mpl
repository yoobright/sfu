# gen_all_coeffs.mpl - Unified Enhanced Minimax Coefficient Generator

with(numapprox):

# Unified bit widths
C0_WIDTH := 26:
C1_WIDTH := 16:
C2_WIDTH := 12:

# Quantize coefficient to integer mantissa
quantize := proc(c_float, width, exp_val)
    local scale, c_mant:
    scale := 2^(width - exp_val):
    c_mant := round(abs(c_float) * scale):
    return c_mant:
end proc:

# Enhanced Minimax for one segment
enhanced_minimax := proc(func_expr, r_low, r_high, m_val, C0_e, C1_e, C2_e)
    local p1, a0, a1, a2, C1_scale, C1_float, a2_prime, C2_scale, C2_float:
    local residual, p2, a0_prime, C0_scale, C0_float:
    local C0_mant, C1_mant, C2_mant:
    
    # Step 1: Initial minimax
    p1 := minimax(func_expr, r = r_low..r_high, 2, 1, 'maxerror'):
    a0 := coeff(p1, r, 0):
    a1 := coeff(p1, r, 1):
    a2 := coeff(p1, r, 2):
    
    # Step 2: Quantize C1 and compensate to C2
    C1_scale := 2^(C1_WIDTH - C1_e):
    C1_float := round(a1 * C1_scale) / C1_scale:
    
    a2_prime := a2 + (a1 - C1_float) * 2^m_val:
    C2_scale := 2^(C2_WIDTH - C2_e):
    C2_float := round(a2_prime * C2_scale) / C2_scale:
    
    # Step 3: Residual minimax for C0
    residual := func_expr - C1_float * r - C2_float * r^2:
    p2 := minimax(residual, r = r_low..r_high, 0, 1, 'maxerror'):
    a0_prime := coeff(p2, r, 0):
    
    C0_scale := 2^(C0_WIDTH - C0_e):
    C0_float := round(a0_prime * C0_scale) / C0_scale:
    
    # Quantize to integer mantissa
    C0_mant := quantize(C0_float, C0_WIDTH, C0_e):
    C1_mant := quantize(C1_float, C1_WIDTH, C1_e):
    C2_mant := quantize(C2_float, C2_WIDTH, C2_e):
    
    return [C0_mant, C1_mant, C2_mant]:
end proc:

# Generate coefficients for one function
generate_function := proc(func_name, func_expr_proc, x_base_proc, m_val, C0_e, C1_e, C2_e)
    local num_segs, outfile, seg_idx, x_base, r_low, r_high:
    local func_expr, coeffs, C0_hex, C1_hex, C2_hex:
    
    num_segs := 2^m_val:
    outfile := cat(func_name, "-coeffs.txt"):
    
    printf("Generating %s (m=%d, segments=%d)\n", func_name, m_val, num_segs):
    printf("  C0_exp=%d, C1_exp=%d, C2_exp=%d\n", C0_e, C1_e, C2_e):
    
    fopen(outfile, WRITE):
    
    for seg_idx from 0 to num_segs - 1 do
        x_base := x_base_proc(seg_idx, m_val):
        r_low := 0:
        r_high := 1 / 2^m_val:
        
        func_expr := func_expr_proc(x_base):
        
        coeffs := enhanced_minimax(func_expr, r_low, r_high, m_val, C0_e, C1_e, C2_e):
        
        C0_hex := convert(coeffs[1], hex):
        C1_hex := convert(coeffs[2], hex):
        C2_hex := convert(coeffs[3], hex):
        
        fprintf(outfile, "%s %s %s\n", C0_hex, C1_hex, C2_hex):
        
        if modp(seg_idx, 16) = 0 then
            printf("  %d/%d\n", seg_idx, num_segs):
        end if:
    end do:
    
    fclose(outfile):
    printf("Done: %s\n\n", outfile):
end proc:

# EXP2: 2^x, x in [0,1)
printf("=== EXP2 ===\n"):
generate_function("exp2",
    x_base -> 2^(x_base + r),
    (seg_idx, m) -> seg_idx / 2^m,
    6, 1, 1, -1):

# LOG2: log2(x), x in [1,2)
printf("=== LOG2 ===\n"):
generate_function("log2",
    x_base -> log[2](x_base + r),
    (seg_idx, m) -> 1.0 + seg_idx / 2^m,
    6, 0, 1, 0):

# RCP: 1/x, x in [1,2)
printf("=== RCP ===\n"):
generate_function("rcp", 
    x_base -> 1 / (x_base + r),
    (seg_idx, m) -> 1.0 + seg_idx / 2^m,
    7, 0, 0, 0):

# SQRT-EVEN: sqrt(x), x in [0.25,0.5)
printf("=== SQRT-EVEN ===\n"):
generate_function("sqrt-even",
    x_base -> sqrt(x_base + r) / 2,
    (seg_idx, m) -> 1 + seg_idx / 2^m,
    6, 0, -1, -3):

# SQRT-ODD: sqrt(x), x in [0.5,1)
printf("=== SQRT-ODD ===\n"):
generate_function("sqrt-odd",
    x_base -> sqrt(x_base + r) / sqrt(2),
    (seg_idx, m) -> 1 + seg_idx / 2^m,
    6, 0, -1, -3):

# RSQRT-EVEN: 1/sqrt(x), x in [1,2)
printf("=== RSQRT-EVEN ===\n"):
generate_function("rsqrt-even",
    x_base -> 1 / sqrt(x_base + r),
    (seg_idx, m) -> 1.0 + seg_idx / 2^m,
    6, 0, -1, -1):

# RSQRT-ODD: 1/sqrt(x), x in [2,4)
printf("=== RSQRT-ODD ===\n"):
generate_function("rsqrt-odd",
    x_base -> 1 / sqrt(2) / sqrt(x_base + r),
    (seg_idx, m) -> 1.0 + seg_idx / 2^m,
    6, 0, -1, -1):

printf("=== ALL DONE ===\n"):
