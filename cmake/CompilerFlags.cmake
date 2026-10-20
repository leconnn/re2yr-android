# Compiler settings for the project's own targets.
#
# Floating-point flags matter for determinism: no fused multiply-add
# contraction and no fast-math, so every platform computes the same bits.

function(ra_set_target_options target)
	if(MSVC)
		target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /fp:precise /Zc:__cplusplus)
		target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
	else()
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off -fno-fast-math)
	endif()
endfunction()
