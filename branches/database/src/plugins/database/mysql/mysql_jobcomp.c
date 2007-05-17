/*****************************************************************************\
 *  mysql_jobcomp.c - text file slurm job completion logging plugin.
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#   include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include "mysql_common.h"
#include "mysql_jobcomp_process.h"
#include <pwd.h>
#include <sys/types.h>
#include "src/common/parse_time.h"

#ifdef HAVE_MYSQL

#define DEFAULT_JOBCOMP_DB "slurm_jobcomp_db"

MYSQL *jobcomp_mysql_db = NULL;
int jobcomp_db_init = 0;

char *jobcomp_table = "jobcomp_table";
database_field_t jobcomp_table_fields[] = {
	{ "jobid", "int not null" },
	{ "uid", "smallint unsigned not null" },
	{ "user_name", "tinytext not null" },
	{ "gid", "smallint unsigned not null" },
	{ "group_name", "tinytext not null" },
	{ "name", "tinytext not null" },
	{ "state", "smallint not null" },
	{ "partition", "tinytext not null" }, 
	{ "timelimit", "tinytext not null" },
	{ "starttime", "int unsigned default 0" }, 
	{ "endtime", "int unsigned default 0" },
	{ "nodelist", "text" }, 
	{ "nodecnt", "mediumint unsigned not null" },
	{ "connection", "tinytext" },
	{ "reboot", "tinytext" },
	{ "rotate", "tinytext" },
	{ "maxprocs", "tinytext" },
	{ "geometry", "tinytext" },
	{ "start", "tinytext" },
	{ "blockid", "tinytext" },
	{ NULL, NULL}
};


/* Type for error string table entries */
typedef struct {
	int xe_number;
	char *xe_message;
} slurm_errtab_t;

static slurm_errtab_t slurm_errtab[] = {
	{0, "No error"},
	{-1, "Unspecified error"}
};

/* A plugin-global errno. */
static int plugin_errno = SLURM_SUCCESS;

/* File descriptor used for logging */
static pthread_mutex_t  jobcomp_lock = PTHREAD_MUTEX_INITIALIZER;


static int _mysql_jobcomp_check_tables()
{
	if(mysql_db_create_table(jobcomp_mysql_db, jobcomp_db_init, 
				 jobcomp_table, jobcomp_table_fields,
				 ")") == SLURM_ERROR)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}


/* get the user name for the give user_id */
static char *_get_user_name(uint32_t user_id)
{
	static uint32_t cache_uid      = 0;
	static char     cache_name[32] = "root";
	struct passwd * user_info      = NULL;
	char *ret_name = NULL;

	slurm_mutex_lock(&jobcomp_lock);
	if (user_id != cache_uid) {
		user_info = getpwuid((uid_t) user_id);
		if (user_info && user_info->pw_name[0])
			snprintf(cache_name, sizeof(cache_name), "%s", 
				user_info->pw_name);
		else
			snprintf(cache_name, sizeof(cache_name), "Unknown");
		cache_uid = user_id;
	}
	ret_name = xstrdup(cache_name);
	slurm_mutex_unlock(&jobcomp_lock);

	return ret_name;
}

/* 
 * Linear search through table of errno values and strings,
 * returns NULL on error, string on success.
 */
static char *_lookup_slurm_api_errtab(int errnum)
{
	char *res = NULL;
	int i;

	for (i = 0; i < sizeof(slurm_errtab) / sizeof(slurm_errtab_t); i++) {
		if (slurm_errtab[i].xe_number == errnum) {
			res = slurm_errtab[i].xe_message;
			break;
		}
	}
	return res;
}

extern int mysql_jobcomp_init(char *location)
{
	mysql_db_info_t *db_info = create_mysql_db_info();
	int rc = SLURM_SUCCESS;
	char *db_name = NULL;
	int i = 0;
	
	if(jobcomp_db_init) 
		return SLURM_ERROR;
	
	if(!location)
		db_name = DEFAULT_JOBCOMP_DB;
	else {
		while(location[i]) {
			if(location[i] == '.' || location[i] == '/') {
				debug("%s doesn't look like a database "
				      "name using %s",
				      location, DEFAULT_JOBCOMP_DB);
				break;
			}
			i++;
		}
		if(location[i]) 
			db_name = DEFAULT_JOBCOMP_DB;
		else
			db_name = location;
	}
		
	debug2("mysql_connect() called for db %s", db_name);
	
	mysql_get_db_connection(&jobcomp_mysql_db, db_name, db_info,
				&jobcomp_db_init);
	
	_mysql_jobcomp_check_tables();

	destroy_mysql_db_info(db_info);

	debug("Jobcomp database init finished");

	return rc;
}

extern int mysql_jobcomp_fini()
{
	if (jobcomp_mysql_db) {
		mysql_close(jobcomp_mysql_db);
		jobcomp_mysql_db = NULL;
	}
	jobcomp_db_init = 0;
	return SLURM_SUCCESS;
}

extern int mysql_jobcomp_get_errno()
{
	return plugin_errno;
}

extern int mysql_jobcomp_log_record(struct job_record *job_ptr)
{
	int rc = SLURM_SUCCESS;
	char *usr_str = NULL, lim_str[32];
#ifdef HAVE_BG
	char connection[128];
	char reboot[4];
	char rotate[4];
	char maxprocs[20];
	char geometry[20];
	char start[20];
	char blockid[128];
#endif
	enum job_states job_state;
	char query[1024];

	if(!jobcomp_mysql_db) {
		char *loc = slurm_get_jobcomp_loc();
		if(mysql_jobcomp_init(loc) == SLURM_ERROR) {
			xfree(loc);
			return SLURM_ERROR;
		}
		xfree(loc);
	}

	usr_str = _get_user_name(job_ptr->user_id);
	if (job_ptr->time_limit == INFINITE)
		strcpy(lim_str, "UNLIMITED");
	else
		snprintf(lim_str, sizeof(lim_str), "%lu", 
				(unsigned long) job_ptr->time_limit);

	/* Job will typically be COMPLETING when this is called. 
	 * We remove this flag to get the eventual completion state:
	 * JOB_FAILED, JOB_TIMEOUT, etc. */
	job_state = job_ptr->job_state & (~JOB_COMPLETING);

#ifdef HAVE_BG
	select_g_sprint_jobinfo(job_ptr->select_jobinfo,
		connection, sizeof(connection), SELECT_PRINT_CONNECTION);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo,
		reboot, sizeof(reboot), SELECT_PRINT_REBOOT);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo,
		rotate, sizeof(rotate), SELECT_PRINT_ROTATE);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo,
		maxprocs, sizeof(maxprocs), SELECT_PRINT_MAX_PROCS);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo,
		geometry, sizeof(geometry), SELECT_PRINT_GEOMETRY);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo,
		start, sizeof(start), SELECT_PRINT_START);
	select_g_sprint_jobinfo(job_ptr->select_jobinfo,
		blockid, sizeof(blockid), SELECT_PRINT_BG_ID);
#endif
	snprintf(query, sizeof(query),
		 "insert into %s (jobid, uid, user_name, name, state, "
		 "partition, timelimit, starttime, endtime, nodelist, nodecnt"
#ifdef HAVE_BG
		 ", connection, reboot, rotate, maxprocs, geometry, "
		 "start, blockid"
#endif
		 ") values (%u, %u, '%s', '%s', %d, '%s', '%s', %u, %u, "
		 "'%s', %u"
#ifdef HAVE_BG
		 ", '%s', '%s', '%s', '%s', '%s', '%s', '%s'"
#endif
		 ")",
		 jobcomp_table, job_ptr->job_id, job_ptr->user_id, usr_str,
		 job_ptr->name, job_state, job_ptr->partition, lim_str,
		 (int)job_ptr->start_time, (int)job_ptr->end_time,
		 job_ptr->nodes, job_ptr->node_cnt
#ifdef HAVE_BG
		 , connection, reboot, rotate, maxprocs, geometry,
		 start, blockid
#endif
		 );
	rc = mysql_db_query(jobcomp_mysql_db, jobcomp_db_init, query);
	xfree(usr_str);

	return rc;
}

extern char *mysql_jobcomp_strerror( int errnum )
{
	char *res = _lookup_slurm_api_errtab(errnum);
	return (res ? res : strerror(errnum));
}

/* 
 * get info from the database 
 * in/out job_list List of job_rec_t *
 * note List needs to be freed when called
 */
extern void mysql_jobcomp_get_jobs(List job_list, 
				      List selected_steps, List selected_parts,
				      void *params)
{
	mysql_jobcomp_process_get_jobs(job_list, 
					  selected_steps, selected_parts,
					  params);	
	return;
}

/* 
 * expire old info from the database 
 */
extern void mysql_jobcomp_archive(List selected_parts,
				     void *params)
{
	mysql_jobcomp_process_archive(selected_parts, params);
	return;
}

#endif
