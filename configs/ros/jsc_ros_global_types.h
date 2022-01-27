/***********************************************************************
 * Copyright 2020 by the California Institute of Technology
 * ALL RIGHTS RESERVED. United States Government Sponsorship acknowledged.
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
 *
 * @file        jsc_ros_global_types.h
 * @date        2020-06-09
 * @author      Neil Abcouwer
 * @brief       Definition of global types for testing jsc
 *
 * jsc_conf_types_pub.h defines public types used by public jsc functions.
 * and follows the common guideline, as expressed in MISRA
 * directive 4.6, "typedefs that indicate size and signedness should be used
 * in place of the basic numerical types".
 *
 * jsc_types_pub.h includes jsc_conf_global_types.h, which must define
 * these types.  For the purposes of working with ROS,
 * test/jsc_ros_global_types.h is copied to
 * include/jsc/jsc_conf_global_types.h.
 * It includes <ros/types.h>.
 *
 */

#ifndef JSC_CONF_GLOBAL_TYPES_H
#define JSC_CONF_GLOBAL_TYPES_H

#include <ros/types.h> // defines fixed-sized types

// throw a compilation error if test is not true
#define JSC_COMPILE_ASSERT(test, msg) \
  typedef U8 (msg)[ ((test) ? 1 : -1) ]

typedef int16_t I16;
typedef int32_t I32;
typedef uint8_t U8;
typedef uint16_t U16;
typedef uint32_t U32;

JSC_COMPILE_ASSERT(sizeof(I16) == 2, I16BadSize);
JSC_COMPILE_ASSERT(sizeof(I32) == 4, I32BadSize);
JSC_COMPILE_ASSERT(sizeof(U8)  == 1,  U8BadSize);
JSC_COMPILE_ASSERT(sizeof(U16) == 2, U16BadSize);
JSC_COMPILE_ASSERT(sizeof(U32) == 4, U32BadSize);

#endif /* JSC_CONF_GLOBAL_TYPES_H */
