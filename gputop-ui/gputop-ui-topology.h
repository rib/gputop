/*
 * GPU Top
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __GPUTOP_UI_TOPOLOGY_H__
#define __GPUTOP_UI_TOPOLOGY_H__

#include <stdint.h>

#include "imgui.h"

namespace Gputop {

void RcsTopology(const char *label,
                 int s_max, int ss_max, int eu_max,
                 const uint8_t *s_mask, const uint8_t *ss_mask, const uint8_t *eus_mask,
                 float width = 0.0f);
void EngineTopology(const char *label,
                    uint32_t n_engines, const uint32_t *engines, const char **names,
                    float width = 0.0f);

};

#endif /* __GPUTOP_UI_TOPOLOGY_H__ */
