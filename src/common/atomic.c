/*****************************************************************************\
 *  atomic.c - Slurm atomics handlers and helpers
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/common/atomic.h"
#include "src/common/log.h"

#ifdef __STDC_NO_ATOMICS__

#include "src/common/macros.h"

extern bool atomic_bool_ptr_set_true(atomic_bool_t *target)
{
	bool value;

	slurm_mutex_lock(&target->mutex);

	value = target->value;
	target->value = true;

	slurm_mutex_unlock(&target->mutex);

	return value;
}

extern bool atomic_bool_ptr_set_true_from_false(atomic_bool_t *target)
{
	bool changed = false;

	slurm_mutex_lock(&target->mutex);

	if (!target->value) {
		target->value = true;
		changed = true;
	}

	slurm_mutex_unlock(&target->mutex);

	return changed;
}

extern bool atomic_bool_ptr_set_false_from_true(atomic_bool_t *target)
{
	bool changed = false;

	slurm_mutex_lock(&target->mutex);

	if (target->value) {
		target->value = false;
		changed = true;
	}

	slurm_mutex_unlock(&target->mutex);

	return changed;
}

extern bool atomic_bool_ptr_get(atomic_bool_t *target)
{
	bool value;

	slurm_mutex_lock(&target->mutex);

	value = target->value;

	slurm_mutex_unlock(&target->mutex);

	return value;
}

extern int32_t atomic_int32_ptr_add(atomic_int32_t *target, int32_t value)
{
	int32_t prior;

	slurm_mutex_lock(&target->mutex);

	prior = target->value;
	target->value += value;

	slurm_mutex_unlock(&target->mutex);

	return prior;
}

extern void atomic_int32_ptr_increment(atomic_int32_t *target)
{
	slurm_mutex_lock(&target->mutex);

	target->value++;

	slurm_mutex_unlock(&target->mutex);
}

extern void atomic_int32_ptr_decrement(atomic_int32_t *target)
{
	slurm_mutex_lock(&target->mutex);

	target->value--;

	slurm_mutex_unlock(&target->mutex);
}

extern int32_t atomic_int32_ptr_get(atomic_int32_t *target)
{
	int32_t value;

	slurm_mutex_lock(&target->mutex);

	value = target->value;

	slurm_mutex_unlock(&target->mutex);

	return value;
}

#else /* !__STDC_NO_ATOMICS__ */

#include <stdatomic.h>

extern bool atomic_bool_ptr_set_true(atomic_bool_t *target)
{
	return atomic_exchange(&target->value, true);
}

extern bool atomic_bool_ptr_set_false(atomic_bool_t *target)
{
	return atomic_exchange(&target->value, false);
}

extern bool atomic_bool_ptr_set_true_from_false(atomic_bool_t *target)
{
	bool expected = false;

	if (atomic_compare_exchange_strong(&target->value, &expected, true))
		return true;

	if (expected == true)
		return false;

	fatal_abort("should never happen: expected=%c", BOOL_CHARIFY(expected));
}

extern bool atomic_bool_ptr_set_false_from_true(atomic_bool_t *target)
{
	bool expected = true;

	if (atomic_compare_exchange_strong(&target->value, &expected, false))
		return true;

	if (expected == false)
		return false;

	fatal_abort("should never happen: expected=%c", BOOL_CHARIFY(expected));
}

extern bool atomic_bool_ptr_get(atomic_bool_t *target)
{
	return atomic_load(&target->value);
}

extern int32_t atomic_int32_ptr_add(atomic_int32_t *target, int32_t value)
{
	return atomic_fetch_add(&target->value, value);
}

extern void atomic_int32_ptr_increment(atomic_int32_t *target)
{
	(void) atomic_fetch_add(&target->value, 1);
}

extern void atomic_int32_ptr_decrement(atomic_int32_t *target)
{
	(void) atomic_fetch_sub(&target->value, 1);
}

extern int32_t atomic_int32_ptr_get(atomic_int32_t *target)
{
	return atomic_load(&target->value);
}

extern int32_t atomic_int32_ptr_set(atomic_int32_t *target, int32_t value)
{
	return atomic_exchange(&target->value, value);
}

#endif /* !__STDC_NO_ATOMICS__ */
