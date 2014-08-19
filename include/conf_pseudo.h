/*
 *  ircd-hybrid: an advanced, lightweight Internet Relay Chat Daemon (ircd)
 *
 *  Copyright (c) 1997-2014 ircd-hybrid development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 *  USA
 */

/*! \file conf_pseudo.h
 * \brief Handles with pseudo commands/service aliases.
 * \version $Id: conf_pseudo.h 4299 2014-07-20 13:51:28Z michael $
 */

#ifndef INCLUDED_conf_pseudo_h
#define INCLUDED_conf_pseudo_h

extern void pseudo_register(const char *, const char *,
                            const char *, const char *,
                            const char *);
extern void pseudo_clear(void);
#endif