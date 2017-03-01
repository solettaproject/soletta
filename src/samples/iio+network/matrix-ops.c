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

#include "matrix-ops.h"
#include <math.h>


void transpose (int rows, int cols, double m[rows][cols], double m_trans[cols][rows])
{
    int i,j;

    for (i = 0; i < rows; i++)
        for (j = 0; j < cols; j++)
            m_trans[j][i] = m[i][j];
}


void multiply (int m, int n, int p, double m1[m][n], double m2[n][p], double result[m][p])
{
    int i,j,k;

    for (i = 0; i < m; i++)
        for (k = 0; k < p; k++) {
            result [i][k] = 0;
            for (j = 0; j < n; j++)
                result [i][k] += m1[i][j] * m2 [j][k];
        }
}


void invert (int s, double m[s][s],  double m_inv[s][s])
{
    double t;
    int swap,i,j,k;
    double tmp[s][s];

    for (i = 0; i < s; i++)
        for (j = 0; j < s; j++)
            m_inv[i][j] = 0;

    for (i = 0; i < s; i++)
        m_inv[i][i] = 1;

    assign(s,s,m,tmp);

    for (i = 0; i < s; i++) {
        swap = i;
        for (j = i+1; j < s; j++) {
            if (fabs(tmp[i][j]) > fabs(tmp[i][i]))
                swap = j;
        }

        if (swap != i) {
            /* swap rows */
            for (k = 0; k < s; k++) {
                t = tmp[k][i];
                tmp[k][i] = tmp[k][swap];
                tmp[k][swap] = t;

                t = m_inv[k][i];
                m_inv[k][i] = m_inv[k][swap];
                m_inv[k][swap] = t;
            }
        }

        t = 1 / tmp[i][i];

        for (k = 0 ; k < s ; k++) {
            tmp[k][i] *= t;
            m_inv[k][i] *= t;
        }

        for (j = 0 ; j < s ; j++)
            if (j != i) {
                t = tmp[i][j];
                for (k = 0 ; k < s; k++) {
                    tmp[k][j] -= tmp[k][i] * t;
                    m_inv[k][j] -= m_inv[k][i] * t;
                }
            }
    }
}


void multiply_scalar_inplace(int rows, int cols, double m[rows][cols], double scalar)
{
    int i,j;

    for (i = 0; i < rows; i++)
        for (j = 0; j < cols; j++)
            m[i][j] = m[i][j] * scalar;
}


void assign (int rows, int cols, double m[rows][cols], double m1[rows][cols])
{
    int i,j;

    for (i = 0; i < rows; i++)
        for (j = 0; j < cols; j++)
            m1[i][j] = m[i][j];
}

void substract (int rows, int cols, double m1[rows][cols], double m2[rows][cols], double res[rows][cols])
{
    int i,j;

    for (i = 0; i < rows; i++)
        for (j = 0; j < cols; j++)
            res[i][j] = m1[i][j] - m2[i][j];
}
