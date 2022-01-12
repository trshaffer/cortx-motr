/* -*- C -*- */
/*
 * Copyright (c) 2013-2020 Seagate Technology LLC and/or its Affiliates
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
 * For any questions about this software or licensing,
 * please email opensource@seagate.com or cortx-questions@seagate.com.
 *
 */

#pragma once

#ifndef __MOTR___DTM0_CO_FOM_H__
#define __MOTR___DTM0_CO_FOM_H__

#include "lib/coroutine.h" /* m0_co_context */
#include "be/op.h"         /* m0_be_op */
#include "fop/fom.h"       /* m0_fom */

/**
 * @defgroup co_fom
 *
 * @{
 */

struct m0_co_fom;

struct m0_co_fom_cfg {
	void (*cf_tick)(struct m0_co_fom *cf, void *cf_arg);
	void *cf_arg;
};

struct m0_co_fom {
	struct m0_be_op      cf_op;
	struct m0_fom        cf_base;
	struct m0_co_context cf_context;
	struct m0_co_fom_cfg cf_cfg;
};

M0_INTERNAL int m0_co_fom_spawn(struct m0_co_fom *cf,
				struct m0_co_fom_cfg *cf_cfg);


M0_INTERNAL int m0_co_fom_init(struct m0_co_fom *cf,
			       struct m0_co_fom_cfg *cf_cfg);
M0_INTERNAL void m0_co_fom_start(struct m0_co_fom *cf);
M0_INTERNAL int m0_co_fom_await(struct m0_co_fom *cf);
M0_INTERNAL int m0_co_fom_failed(struct m0_co_fom *cf, int rc);

#define M0_CO_FOM_CONTEXT(co_fom) (&(co_fom)->cf_context)
#define M0_CO_FOM_AWAIT(co_fom) \
	M0_CO_YIELD_RC(M0_CO_FOM_CONTEXT(co_fom), m0_co_fom_await(co_fom))

/** @} end of co_fom group */
#endif /* __MOTR___DTM0_CO_FOM_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
