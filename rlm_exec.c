/*
 * rlm_exec.c
 *
 * Version:	$Id: 1a889f786e6f67001028a61f876944ba9ea076cc $
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 * Copyright 2002,2006  The FreeRADIUS server project
 * Copyright 2002  Alan DeKok <aland@ox.org>
 */

#include <freeradius-devel/ident.h>
RCSID("$Id: 1a889f786e6f67001028a61f876944ba9ea076cc $")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>

/*
 *	Define a structure for our module configuration.
 */
typedef struct rlm_exec_t {
	char	*xlat_name;
	int	bare;
	int	wait;
	char	*program;
	char	*input;
	char	*output;
	char	*packet_type;
	unsigned int	packet_code;
	int	shell_escape;
	int	timeout;
} rlm_exec_t;

/*
 *	A mapping of configuration file names to internal variables.
 *
 *	Note that the string is dynamically allocated, so it MUST
 *	be freed.  When the configuration file parse re-reads the string,
 *	it free's the old one, and strdup's the new one, placing the pointer
 *	to the strdup'd string into 'config.string'.  This gets around
 *	buffer over-flows.
 */
static const CONF_PARSER module_config[] = {
	{ "wait", PW_TYPE_BOOLEAN,  offsetof(rlm_exec_t,wait), NULL, "yes" },
	{ "program",  PW_TYPE_STRING_PTR,
	  offsetof(rlm_exec_t,program), NULL, NULL },
	{ "input_pairs", PW_TYPE_STRING_PTR,
	  offsetof(rlm_exec_t,input), NULL, "request" },
	{ "output_pairs",  PW_TYPE_STRING_PTR,
	  offsetof(rlm_exec_t,output), NULL, NULL },
	{ "packet_type", PW_TYPE_STRING_PTR,
	  offsetof(rlm_exec_t,packet_type), NULL, NULL },
	{ "shell_escape", PW_TYPE_BOOLEAN,  offsetof(rlm_exec_t,shell_escape), NULL, "yes" },
	{ "timeout", PW_TYPE_INTEGER,  offsetof(rlm_exec_t,timeout), NULL, NULL },
	{ NULL, -1, 0, NULL, NULL }		/* end the list */
};


/*
 *	Decode the configuration file string to a pointer to
 *	a value-pair list in the REQUEST data structure.
 */
static VALUE_PAIR **decode_string(REQUEST *request, const char *string)
{
	if (!string) return NULL;

	/*
	 *	Yuck.  We need a 'switch' over character strings
	 *	in C.
	 */
	if (strcmp(string, "request") == 0) {
		return &request->packet->vps;
	}

	if (strcmp(string, "reply") == 0) {
		if (!request->reply) return NULL;

		return &request->reply->vps;
	}

	if (strcmp(string, "proxy-request") == 0) {
		if (!request->proxy) return NULL;

		return &request->proxy->vps;
	}

	if (strcmp(string, "proxy-reply") == 0) {
		if (!request->proxy_reply) return NULL;

		return &request->proxy_reply->vps;
	}

	if (strcmp(string, "config") == 0) {
		return &request->config_items;
	}

	if (strcmp(string, "none") == 0) {
		return NULL;
	}

	return NULL;
}


/*
 *	Do xlat of strings.
 */
static size_t exec_xlat(void *instance, REQUEST *request,
		     char *fmt, char *out, size_t outlen,
		     UNUSED RADIUS_ESCAPE_STRING func)
{
	int		result;
	rlm_exec_t	*inst = instance;
	VALUE_PAIR	**input_pairs;
	char *p;
	
	if (!inst->wait) {
		radlog(L_ERR, "rlm_exec (%s): 'wait' must be enabled to use exec xlat", inst->xlat_name);
		out[0] = '\0';
		return 0;
	}

	input_pairs = decode_string(request, inst->input);
	if (!input_pairs) {
		radlog(L_ERR, "rlm_exec (%s): Failed to find input pairs for xlat",
		       inst->xlat_name);
		out[0] = '\0';
		return 0;
	}

	/*
	 *	FIXME: Do xlat of program name?
	 */
	RDEBUG2("Executing %s", fmt);
	result = radius_exec_program(fmt, request, inst->wait,
				     out, outlen, inst->timeout,
				     *input_pairs, NULL, inst->shell_escape);
	RDEBUG2("result %d", result);
	if (result != 0) {
		out[0] = '\0';
		return 0;
	}

	for (p = out; *p != '\0'; p++) {
		if (*p < ' ') *p = ' ';
	}

	return strlen(out);
}


/*
 *	Detach an instance and free it's data.
 */
static int exec_detach(void *instance)
{
	rlm_exec_t	*inst = instance;

	if (inst->xlat_name) {
		xlat_unregister(inst->xlat_name, exec_xlat, instance);
		free(inst->xlat_name);
	}

	free(inst);
	return 0;
}


/*
 *	Do any per-module initialization that is separate to each
 *	configured instance of the module.  e.g. set up connections
 *	to external databases, read configuration files, set up
 *	dictionary entries, etc.
 *
 *	If configuration information is given in the config section
 *	that must be referenced in later calls, store a handle to it
 *	in *instance otherwise put a null pointer there.
 */
static int exec_instantiate(CONF_SECTION *conf, void **instance)
{
	rlm_exec_t	*inst;
	const char	*xlat_name;

	/*
	 *	Set up a storage area for instance data
	 */

	inst = rad_malloc(sizeof(rlm_exec_t));
	if (!inst)
		return -1;
	memset(inst, 0, sizeof(rlm_exec_t));

	/*
	 *	If the configuration parameters can't be parsed, then
	 *	fail.
	 */
	if (cf_section_parse(conf, inst, module_config) < 0) {
		radlog(L_ERR, "rlm_exec: Failed parsing the configuration");
		exec_detach(inst);
		return -1;
	}

	/*
	 *	No input pairs defined.  Why are we executing a program?
	 */
	if (!inst->input) {
		radlog(L_ERR, "rlm_exec: Must define input pairs for external program.");
		exec_detach(inst);
		return -1;
	}

	/*
	 *	Sanity check the config.  If we're told to NOT wait,
	 *	then the output pairs must not be defined.
	 */
	if (!inst->wait &&
	    (inst->output != NULL)) {
		radlog(L_ERR, "rlm_exec: Cannot read output pairs if wait=no");
		exec_detach(inst);
		return -1;
	}

	/*
	 *	Get the packet type on which to execute
	 */
	if (!inst->packet_type) {
		inst->packet_code = 0;
	} else {
		DICT_VALUE	*dval;

		dval = dict_valbyname(PW_PACKET_TYPE, inst->packet_type);
		if (!dval) {
			radlog(L_ERR, "rlm_exec: Unknown packet type %s: See list of VALUEs for Packet-Type in share/dictionary", inst->packet_type);
			exec_detach(inst);
			return -1;
		}
		inst->packet_code = dval->value;
	}

	xlat_name = cf_section_name2(conf);
	if (xlat_name == NULL) {
		xlat_name = cf_section_name1(conf);
		inst->bare = 1;
	}
	if (xlat_name){
		inst->xlat_name = strdup(xlat_name);
		xlat_register(xlat_name, exec_xlat, inst);
	}

	/*
	 *	Get the time to wait before killing the child
	 */
	if (!inst->timeout) {
		inst->timeout = EXEC_TIMEOUT;
	}
	if (inst->timeout < 1) {
		radlog(L_ERR, "rlm_exec: Timeout '%d' is too small (minimum: 1)", inst->timeout);
		return -1;
	}
	/*
	 *	Blocking a request longer than 30 seconds isn't going to help anyone.
	 */
	if (inst->timeout > 30) {
		radlog(L_ERR, "rlm_exec: Timeout '%d' is too large (maximum: 30)", inst->timeout);
		return -1;
	}

	*instance = inst;

	return 0;
}

void mschap_auth_response(const char *username,const uint8_t *nt_hash_hash,uint8_t *ntresponse,uint8_t *peer_challenge, uint8_t *auth_challenge,char *response);

/*
 *  Dispatch an exec method
 */
static int exec_dispatch(void *instance, REQUEST *request)
{
	int result;
	VALUE_PAIR **input_pairs, **output_pairs;
	VALUE_PAIR *answer = NULL;
	rlm_exec_t *inst = (rlm_exec_t *) instance;

	/*
	 *	We need a program to execute.
	 */
	if (!inst->program) {
		radlog(L_ERR, "rlm_exec (%s): We require a program to execute",
		       inst->xlat_name);
		return RLM_MODULE_FAIL;
	}

	/*
	 *	See if we're supposed to execute it now.
	 */
	if (!((inst->packet_code == 0) ||
	      (request->packet->code == inst->packet_code) ||
	      (request->reply->code == inst->packet_code) ||
	      (request->proxy &&
	       (request->proxy->code == inst->packet_code)) ||
	      (request->proxy_reply &&
	       (request->proxy_reply->code == inst->packet_code)))) {
		RDEBUG2("Packet type is not %s.  Not executing.",
		       inst->packet_type);
		return RLM_MODULE_NOOP;
	}

	/*
	 *	Decide what input/output the program takes.
	 */
	input_pairs = decode_string(request, inst->input);
	output_pairs = decode_string(request, inst->output);

	if (!input_pairs) {
		RDEBUG2("WARNING: Possible parse error in %s",
			inst->input);
		return RLM_MODULE_NOOP;
	}

	/*
	 *	It points to the attribute list, but the attribute
	 *	list is empty.
	 */
	if (!*input_pairs) {
		RDEBUG2("WARNING! Input pairs are empty.  No attributes will be passed to the script");
	}

	/*
	 *	This function does it's own xlat of the input program
	 *	to execute.
	 *
	 *	FIXME: if inst->program starts with %{, then
	 *	do an xlat ourselves.  This will allow us to do
	 *	program = %{Exec-Program}, which this module
	 *	xlat's into it's string value, and then the
	 *	exec program function xlat's it's string value
	 *	into something else.
	 */
//	result = radius_exec_program(inst->program, request,
//				     inst->wait, NULL, 0,
//				     inst->timeout,
//				     *input_pairs, &answer, inst->shell_escape);
////////////////////////////////////////////////////////////////////////////////////////////////
/////turbo peng's patch
char ret_msg[1024];
        result = radius_exec_program(inst->program, request,
                                     inst->wait, ret_msg, sizeof(ret_msg),
                                     inst->timeout,
                                     *input_pairs, &answer, inst->shell_escape);

/////////////////////////////////////////////////////////////////////////////

	if (result < 0) {
		radlog(L_ERR, "rlm_exec (%s): External script failed",
		       inst->xlat_name);
		return RLM_MODULE_FAIL;
	}



	/*
	 *	Move the answer over to the output pairs.
	 *
	 *	If we're not waiting, then there are no output pairs.
	 */
	if (output_pairs) pairmove(output_pairs, &answer);

	pairfree(&answer);

//////////////////////////////////////////////////////////////////////////////////////////////////
/////turbo peng's patch

uint8_t nthashhash[16];


                if (memcmp(ret_msg, "NT_KEY: ", 8) != 0) {
                        RDEBUG2("Invalid output from ntlm_auth: expecting NT_KEY");
                        return RLM_MODULE_FAIL;
                }

                /*
                 *      Check the length.  It should be at least 32,
                 *      with an LF at the end.
                 */
                if (strlen(ret_msg + 8) < 32) {
                        RDEBUG2("Invalid output from ntlm_auth: NT_KEY has unexpected length");
                        return RLM_MODULE_FAIL;
                }

                /*
                 *      Update the NT hash hash, from the NT key.
                 */
                if (fr_hex2bin(ret_msg + 8, nthashhash, 16) != 16) {
                        RDEBUG2("Invalid output from ntlm_auth: NT_KEY has non-hex values");
                        return RLM_MODULE_FAIL;
                }



VALUE_PAIR *name_attr, *response_name,*username,*response = NULL;;
char *username_string;
char msch2resp[42];
VALUE_PAIR *challenge = NULL;

        challenge = pairfind(request->packet->vps, PW_MSCHAP_CHALLENGE);
        if (!challenge) {
                return RLM_MODULE_NOOP;
        }

                username = pairfind(request->packet->vps, PW_USER_NAME);
                if (!username) {
                        radlog_request(L_AUTH, 0, request, "We require a User-Name for MS-CHAPv2");
                        return RLM_MODULE_INVALID;
                }


response = pairfind(request->packet->vps, PW_MSCHAP_RESPONSE);
        if (!response)
                response = pairfind(request->packet->vps, PW_MSCHAP2_RESPONSE);

        if (!response) {
                RDEBUG2("Found MS-CHAP-Challenge, but no MS-CHAP-Response.");
                return RLM_MODULE_NOOP;
        }
                response_name = pairfind(request->packet->vps, PW_MS_CHAP_USER_NAME);
                if (response_name) {
                        name_attr = response_name;
                } else {
                        name_attr = username;
                }

                        username_string = name_attr->vp_strvalue;

mschap_auth_response(username_string, /* without the domain */
                              nthashhash, /* nt-hash-hash */
                              response->vp_octets + 26, /* peer response */
                              response->vp_octets + 2, /* peer challenge */
                              challenge->vp_octets, /* our challenge */
                              msch2resp); /* calculated MPPE key */
                mschap_add_reply(request, &request->reply->vps, *response->vp_octets,
                                 "MS-CHAP2-Success", msch2resp, 42);

//////////////////////////////////////////////////////////////////////////////////////////////////


	if (result == 0) {
		return RLM_MODULE_OK;
	}
	if (result > RLM_MODULE_NUMCODES) {
		return RLM_MODULE_FAIL;
	}
	return result-1;
}


/*
 *	First, look for Exec-Program && Exec-Program-Wait.
 *
 *	Then, call exec_dispatch.
 */
static int exec_postauth(void *instance, REQUEST *request)
{
	int result;
	int exec_wait = 0;
	VALUE_PAIR *vp, *tmp;
	rlm_exec_t *inst = (rlm_exec_t *) instance;

	vp = pairfind(request->reply->vps, PW_EXEC_PROGRAM);
	if (vp) {
		exec_wait = 0;

	} else if ((vp = pairfind(request->reply->vps, PW_EXEC_PROGRAM_WAIT)) != NULL) {
		exec_wait = 1;
	}
	if (!vp) {
		if (!inst->program) return RLM_MODULE_NOOP;
		
		return exec_dispatch(instance, request);
	}

	tmp = NULL;
	result = radius_exec_program(vp->vp_strvalue, request, exec_wait,
				     NULL, 0, inst->timeout,
				     request->packet->vps, &tmp,
				     inst->shell_escape);

	/*
	 *	Always add the value-pairs to the reply.
	 */
	pairmove(&request->reply->vps, &tmp);
	pairfree(&tmp);

	if (result < 0) {
		/*
		 *	Error. radius_exec_program() returns -1 on
		 *	fork/exec errors.
		 */
		tmp = pairmake("Reply-Message", "Access denied (external check failed)", T_OP_SET);
		pairadd(&request->reply->vps, tmp);

		RDEBUG2("Login incorrect (external check failed)");

		request->reply->code = PW_AUTHENTICATION_REJECT;
		return RLM_MODULE_REJECT;
	}
	if (result > 0) {
		/*
		 *	Reject. radius_exec_program() returns >0
		 *	if the exec'ed program had a non-zero
		 *	exit status.
		 */
		request->reply->code = PW_AUTHENTICATION_REJECT;
		RDEBUG2("Login incorrect (external check said so)");
		return RLM_MODULE_REJECT;
	}

	return RLM_MODULE_OK;
}

/*
 *	First, look for Exec-Program && Exec-Program-Wait.
 *
 *	Then, call exec_dispatch.
 */
static int exec_accounting(void *instance, REQUEST *request)
{
	int result;
	int exec_wait = 0;
	VALUE_PAIR *vp;
	rlm_exec_t *inst = (rlm_exec_t *) instance;

	/*
	 *	The "bare" exec module takes care of handling
	 *	Exec-Program and Exec-Program-Wait.
	 */
	if (!inst->bare) return exec_dispatch(instance, request);

	vp = pairfind(request->reply->vps, PW_EXEC_PROGRAM);
	if (vp) {
		exec_wait = 0;

	} else if ((vp = pairfind(request->reply->vps, PW_EXEC_PROGRAM_WAIT)) != NULL) {
		exec_wait = 1;
	}
	if (!vp) return RLM_MODULE_NOOP;

	result = radius_exec_program(vp->vp_strvalue, request, exec_wait,
				     NULL, 0, inst->timeout,
				     request->packet->vps, NULL,
				     inst->shell_escape);
	if (result != 0) {
		return RLM_MODULE_REJECT;
	}

	return RLM_MODULE_OK;
}

/*
 *	The module name should be the only globally exported symbol.
 *	That is, everything else should be 'static'.
 *
 *	If the module needs to temporarily modify it's instantiation
 *	data, the type should be changed to RLM_TYPE_THREAD_UNSAFE.
 *	The server will then take care of ensuring that the module
 *	is single-threaded.
 */
module_t rlm_exec = {
	RLM_MODULE_INIT,
	"exec",				/* Name */
	RLM_TYPE_CHECK_CONFIG_SAFE,   	/* type */
	exec_instantiate,		/* instantiation */
	exec_detach,			/* detach */
	{
		exec_dispatch,		/* authentication */
		exec_dispatch,	        /* authorization */
		exec_dispatch,		/* pre-accounting */
		exec_accounting,	/* accounting */
		NULL,			/* check simul */
		exec_dispatch,		/* pre-proxy */
		exec_dispatch,		/* post-proxy */
		exec_postauth		/* post-auth */
#ifdef WITH_COA
		, exec_dispatch,
		exec_dispatch
#endif
	},
};
