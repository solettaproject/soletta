/*
 * This file is part of the Soletta (TM) Project
 *
 * Copyright (C) 2015 Intel Corporation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __MATRIX_OPS__
#define __MATRIX_OPS__

void transpose (int rows, int cols, double m[rows][cols], double m_trans[cols][rows]);
void multiply (int m, int n, int p, double m1[m][n], double m2[n][p], double result[m][p]);
void invert (int s, double m[s][s],  double m_inv[s][s]);
void multiply_scalar_inplace(int rows, int cols, double m[rows][cols], double scalar);
void assign (int rows, int cols, double m[rows][cols], double m1[rows][cols]);
void substract (int rows, int cols, double m1[rows][cols], double m2[rows][cols], double res[rows][cols]);

#endif
