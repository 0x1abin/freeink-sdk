#pragma once

#include <hal/lcd_ll.h>

// IDF 6.1 source and the earlier Arduino RC1 package use different signatures.
// Match the selected build graph explicitly; never cast a pointer into a group.
#if FREEINK_METALIO_PLUS_NATIVE_IDF
#include <sdkconfig.h>
_Static_assert(CONFIG_SPIRAM_SPEED == 200, "Metalio Plus native core must use validated 200MHz PSRAM");
_Static_assert(__builtin_types_compatible_p(__typeof__(&lcd_ll_select_clk_src),
                                            void (*)(lcd_cam_dev_t*, lcd_clock_source_t)),
               "Review the native S31 LCD clock-source API for this framework");
_Static_assert(__builtin_types_compatible_p(__typeof__(&lcd_ll_set_group_clock_coeff),
                                            void (*)(lcd_cam_dev_t*, int, int, int)),
               "Review the native S31 LCD divider API for this framework");
#else
_Static_assert(__builtin_types_compatible_p(__typeof__(&lcd_ll_select_clk_src), void (*)(int, lcd_clock_source_t)),
               "Review the RC1 S31 LCD clock-source API for this framework");
_Static_assert(__builtin_types_compatible_p(__typeof__(&lcd_ll_set_group_clock_coeff), void (*)(int, int, int, int)),
               "Review the RC1 S31 LCD divider API for this framework");
#define lcd_ll_select_clk_src(dev, src) lcd_ll_select_clk_src(0, (src))
#define lcd_ll_set_group_clock_coeff(dev, n, a, b) lcd_ll_set_group_clock_coeff(0, (n), (a), (b))
#endif
