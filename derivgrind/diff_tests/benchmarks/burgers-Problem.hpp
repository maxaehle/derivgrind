
/*
   This file is part of Derivgrind, an automatic differentiation
   tool applicable to compiled programs.

   Copyright (C) 2022, Chair for Scientific Computing, TU Kaiserslautern
   Copyright (C) since 2023, Chair for Scientific Computing, University of Kaiserslautern-Landau
   Homepage: https://www.scicomp.uni-kl.de
   Contact: Prof. Nicolas R. Gauger (derivgrind@projects.rptu.de)

   Lead developer: Max Aehle

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.

   The GNU General Public License is contained in the file COPYING.
*/

/*!
 * Original author: Max Sagebaum
 * Modified by: Max Aehle
 */

#ifndef BURGERS_PROBLEM_HPP
#define BURGERS_PROBLEM_HPP

#include <math.h>
#include <string>
#include <sys/stat.h>
#include <sstream>

/**
 * Conversion function which converts the string into the given type.
 *
 * @param string        The string representation
 *
 * @return  The value from the string representation
 */
template<typename TYPE>
TYPE parseType(const std::string& string) {
    TYPE value;

    std::stringstream ss(string);
    ss >> value;

    return value;
}

template<typename Number, typename NumberOff>
struct Problem {
  struct Settings {
    // values set by user
    size_t gridSize;
    size_t timeSteps;
    NumberOff R;
    NumberOff a;
    NumberOff b;
    NumberOff dT;

    // values which are computed from the above values
    size_t totalSize;
    size_t innerStart;
    size_t innerEnd;
    NumberOff oneOverR;
    NumberOff dX;
    NumberOff dTbyDX;
    NumberOff dTbyDX2;


    void updateDependentValues() {
      totalSize = gridSize * gridSize;
      innerStart = 1;
      if (0 != gridSize) {
        innerEnd = gridSize - 1;
      } else {
        innerEnd = 0;
      }

      oneOverR = 1.0 / R;

      NumberOff length = b - a;
      if (0 != gridSize) {
        dX = length / (NumberOff) (gridSize - 1);
      } else {
        dX = length;
      }
      dTbyDX = dT / dX;
      dTbyDX2 = dT / (dX * dX);
    }
  };

  Number *uStart;
  Number *vStart;
  Number *u1;
  Number *u2;
  Number *v1;
  Number *v2;

  long x;
  long t;

  int runs;
  std::string outDir;
  std::string prefix;


  inline NumberOff evalFuncU(const size_t xPos, const size_t yPos, const NumberOff t, const Settings& props) {
    NumberOff x = xPos * props.dX;
    NumberOff y = yPos * props.dX;

    return (x + y - 2.0 * x * t) / (1.0 - 2.0 * t * t);
  }

  inline NumberOff evalFuncV(const size_t xPos, const size_t yPos, const NumberOff t, const Settings& props) {
    NumberOff x = xPos * props.dX;
    NumberOff y = yPos * props.dX;

    return (x - y - 2.0 * y * t) / (1.0 - 2.0 * t * t);
  }

  inline void setBoundaryConditions(Number *u, Number *v, const NumberOff time, const Settings& props) {
    for (size_t gridPos = 0; gridPos < props.gridSize; ++gridPos) {
      size_t bx0 = gridPos;
      size_t bx1 = gridPos + props.innerEnd * props.gridSize;
      size_t b0y = gridPos * props.gridSize;
      size_t b1y = gridPos * props.gridSize + props.innerEnd;

      u[bx0] = evalFuncU(gridPos, 0, time, props);
      u[bx1] = evalFuncU(gridPos, props.innerEnd, time, props);
      u[b0y] = evalFuncU(0, gridPos, time, props);
      u[b1y] = evalFuncU(props.innerEnd, gridPos, time, props);

      v[bx0] = evalFuncV(gridPos, 0, time, props);
      v[bx1] = evalFuncV(gridPos, props.innerEnd, time, props);
      v[b0y] = evalFuncV(0, gridPos, time, props);
      v[b1y] = evalFuncV(props.innerEnd, gridPos, time, props);
    }
  }

  inline void setInitialConditions(Number *u, Number *v, const Settings& props) {
    for (size_t j = 0; j < props.gridSize; ++j) {
      for (size_t i = 0; i < props.gridSize; ++i) {
        size_t index = i + j * props.gridSize;

        u[index] = evalFuncU(i, j, 0.0, props);
        v[index] = evalFuncV(i, j, 0.0, props);
      }
    }
  }

  inline void updateField(Number *w_tp, const Number *w_t, const Number *u, const Number *v, const Settings& props) {
    // w_t + u*w_x + v*w_y = 1/R(w_xx + w_yy);
    Number velX;
    Number velY;
    Number vis;
    for (size_t j = props.innerStart; j < props.innerEnd; ++j) {
      for (size_t i = props.innerStart; i < props.innerEnd; ++i) {
        size_t index = i + j * props.gridSize;
        size_t index_xp = index + 1;
        size_t index_xm = index - 1;
        size_t index_yp = index + props.gridSize;
        size_t index_ym = index - props.gridSize;

        if (u[index] >= 0.0) {
          velX = u[index] * (w_t[index] - w_t[index_xm]);
        } else {
          velX = u[index] * ( w_t[index_xp] - w_t[index]);
        }
        if (v[index] >= 0.0) {
          velY = v[index] * (w_t[index] - w_t[index_ym]);
        } else {
          velY = v[index] * (w_t[index_yp] - w_t[index]);
        }

        vis = w_t[index_xp] + w_t[index_xm] + w_t[index_yp] + w_t[index_ym] - 4.0 * w_t[index];
        w_tp[index] = w_t[index] - props.dTbyDX * (velX + velY) + props.oneOverR * props.dTbyDX2 * vis;
      }
    }
  }

  inline void doStep(Number *u_cur, Number *u_next, Number *v_cur, Number *v_next, NumberOff& t,
                     const Settings& props) {
    updateField(u_next, u_cur, u_cur, v_cur, props); // update for u
    updateField(v_next, v_cur, u_cur, v_cur, props); // update for v
    t += props.dT;
    setBoundaryConditions(u_next, v_next, t, props);
  }

  void mainLoop(Number *u1, Number *u2, Number *v1, Number *v2, const Settings& props) {
    size_t timeEnd = props.timeSteps / 2; // we do two steps in each iteration
    NumberOff t = 0.0;
    for (size_t time = 0; time < timeEnd; ++time) {
      // first step
      doStep(u1, u2, v1, v2, t, props);

      // second step
      doStep(u2, u1, v2, v1, t, props);
    }
  }

  inline void computeL2Norm(const Number *u, const Number *v, const Settings& props, Number& rValue) {
    Number normU = 0.0;
    Number normV = 0.0;
    for (size_t j = props.innerStart; j < props.innerEnd; ++j) {
      for (size_t i = props.innerStart; i < props.innerEnd; ++i) {
        size_t index = i + j * props.gridSize;
        normU += u[index] * u[index];
        normV += v[index] * v[index];
      }
    }

    Number totalNorm = sqrt(normU) + sqrt(normV);
    rValue = totalNorm / props.totalSize;
  }

  Settings setup(int nArgs, char** args) {
    if (nArgs != 4) {
      std::cerr << "Need 2 arguments: outputfile grid_size time_steps" << std::endl;
      exit(0);
    }

    x = parseType<long>(args[2]);
    t = parseType<long>(args[3]);

    Settings props;
    props.gridSize = x;
    props.timeSteps = t;
    props.R = 1.0;
    props.a = 0.0;
    props.b = 50.0;
    props.dT = 1e-4;
    props.updateDependentValues();

    uStart = new Number[props.totalSize];
    vStart = new Number[props.totalSize];
    u1 = new Number[props.totalSize];
    u2 = new Number[props.totalSize];
    v1 = new Number[props.totalSize];
    v2 = new Number[props.totalSize];

    setInitialConditions(uStart, vStart, props);

    return props;
  }

  void clear() {
    delete[] v2;
    delete[] v1;
    delete[] u2;
    delete[] u1;
    delete[] vStart;
    delete[] uStart;
  }
};

#endif // BURGERS_PROBLEM_HPP
