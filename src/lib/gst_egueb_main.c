/* Gst Egueb - Gstreamer based plugins and libs for Egueb
 * Copyright (C) 2014 Jorge Luis Zapata
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "gst_egueb_private.h"
#include "Gst_Egueb.h"
/*============================================================================*
 *                                  Local                                     *
 *============================================================================*/
static int _init = 0;
/*============================================================================*
 *                                 Global                                     *
 *============================================================================*/
int gst_egueb_log = -1;
/*============================================================================*
 *                                   API                                      *
 *============================================================================*/
EAPI void gst_egueb_init(void)
{
	if (!_init++)
	{
		eina_init();
		gst_egueb_log = eina_log_domain_register("gst-egueb", GST_EGUEB_LOG_COLOR_DEFAULT);
		egueb_dom_init();
		gst_init(NULL, NULL);
	}
}

EAPI void gst_egueb_shutdown(void)
{
	if (_init == 1)
	{
		gst_deinit();
		egueb_dom_shutdown();
		eina_log_domain_unregister(gst_egueb_log);
		eina_shutdown();
	}
	_init--;
}
