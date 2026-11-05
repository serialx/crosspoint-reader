/* 28bcd8b1ba7eb595d82822908257fd9c3589b4243e3c922d0369f35bfcd7b506 (2.7.3+)
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \\  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 1997-2000 Thai Open Source Software Center Ltd
   Copyright (c) 2000      Clark Cooper <coopercc@users.sourceforge.net>
   Copyright (c) 2000-2006 Fred L. Drake, Jr. <fdrake@users.sourceforge.net>
   Copyright (c) 2001-2002 Greg Stein <gstein@users.sourceforge.net>
   Copyright (c) 2002-2016 Karl Waclawek <karl@waclawek.net>
   Copyright (c) 2005-2009 Steven Solie <steven@solie.ca>
   Copyright (c) 2016      Eric Rahm <erahm@mozilla.com>
   Copyright (c) 2016-2025 Sebastian Pipping <sebastian@pipping.org>
   Copyright (c) 2016      Gaurav <g.gupta@samsung.com>
   Copyright (c) 2016      Thomas Beutlich <tc@tbeu.de>
   Copyright (c) 2016      Gustavo Grieco <gustavo.grieco@imag.fr>
   Copyright (c) 2016      Pascal Cuoq <cuoq@trust-in-soft.com>
   Copyright (c) 2016      Ed Schouten <ed@nuxi.nl>
   Copyright (c) 2017-2022 Rhodri James <rhodri@wildebeest.org.uk>
   Copyright (c) 2017      Václav Slavík <vaclav@slavik.io>
   Copyright (c) 2017      Viktor Szakats <commit@vsz.me>
   Copyright (c) 2017      Chanho Park <chanho61.park@samsung.com>
   Copyright (c) 2017      Rolf Eike Beer <eike@sf-mail.de>
   Copyright (c) 2017      Hans Wennborg <hans@chromium.org>
   Copyright (c) 2018      Anton Maklakov <antmak.pub@gmail.com>
   Copyright (c) 2018      Benjamin Peterson <benjamin@python.org>
   Copyright (c) 2018      Marco Maggi <marco.maggi-ipsu@poste.it>
   Copyright (c) 2018      Mariusz Zaborski <oshogbo@vexillium.org>
   Copyright (c) 2019      David Loffredo <loffredo@steptools.com>
   Copyright (c) 2019-2020 Ben Wagner <bungeman@chromium.org>
   Copyright (c) 2019      Vadim Zeitlin <vadim@zeitlins.org>
   Copyright (c) 2021      Donghee Na <donghee.na@python.org>
   Copyright (c) 2022      Samanta Navarro <ferivoz@riseup.net>
   Copyright (c) 2022      Jeffrey Walton <noloader@gmail.com>
   Copyright (c) 2022      Jann Horn <jannh@google.com>
   Copyright (c) 2022      Sean McBride <sean@rogue-research.com>
   Copyright (c) 2023      Owain Davies <owaind@bath.edu>
   Copyright (c) 2023-2024 Sony Corporation / Snild Dolkow <snild@sony.com>
   Copyright (c) 2024-2025 Berkay Eren Ürün <berkay.ueruen@siemens.com>
   Copyright (c) 2024      Hanno Böck <hanno@gentoo.org>
   Copyright (c) 2025      Matthew Fernandez <matthew.fernandez@gmail.com>
   Licensed under the MIT license:

   Permission is  hereby granted,  free of charge,  to any  person obtaining
   a  copy  of  this  software   and  associated  documentation  files  (the
   "Software"),  to  deal in  the  Software  without restriction,  including
   without  limitation the  rights  to use,  copy,  modify, merge,  publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit
   persons  to whom  the Software  is  furnished to  do so,  subject to  the
   following conditions:

   The above copyright  notice and this permission notice  shall be included
   in all copies or substantial portions of the Software.

   THE  SOFTWARE  IS  PROVIDED  "AS  IS",  WITHOUT  WARRANTY  OF  ANY  KIND,
   EXPRESS  OR IMPLIED,  INCLUDING  BUT  NOT LIMITED  TO  THE WARRANTIES  OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
   NO EVENT SHALL THE AUTHORS OR  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR  OTHER LIABILITY, WHETHER  IN AN  ACTION OF CONTRACT,  TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

// Error diagnostics retained from Expat 2.7.3; parsing lives in xp-core.
#include "expat.h"

const XML_LChar* XMLCALL XML_ErrorString(enum XML_Error code) {
  switch (code) {
    case XML_ERROR_NONE:
      return nullptr;
    case XML_ERROR_NO_MEMORY:
      return ("out of memory");
    case XML_ERROR_SYNTAX:
      return ("syntax error");
    case XML_ERROR_NO_ELEMENTS:
      return ("no element found");
    case XML_ERROR_INVALID_TOKEN:
      return ("not well-formed (invalid token)");
    case XML_ERROR_UNCLOSED_TOKEN:
      return ("unclosed token");
    case XML_ERROR_PARTIAL_CHAR:
      return ("partial character");
    case XML_ERROR_TAG_MISMATCH:
      return ("mismatched tag");
    case XML_ERROR_DUPLICATE_ATTRIBUTE:
      return ("duplicate attribute");
    case XML_ERROR_JUNK_AFTER_DOC_ELEMENT:
      return ("junk after document element");
    case XML_ERROR_PARAM_ENTITY_REF:
      return ("illegal parameter entity reference");
    case XML_ERROR_UNDEFINED_ENTITY:
      return ("undefined entity");
    case XML_ERROR_RECURSIVE_ENTITY_REF:
      return ("recursive entity reference");
    case XML_ERROR_ASYNC_ENTITY:
      return ("asynchronous entity");
    case XML_ERROR_BAD_CHAR_REF:
      return ("reference to invalid character number");
    case XML_ERROR_BINARY_ENTITY_REF:
      return ("reference to binary entity");
    case XML_ERROR_ATTRIBUTE_EXTERNAL_ENTITY_REF:
      return ("reference to external entity in attribute");
    case XML_ERROR_MISPLACED_XML_PI:
      return ("XML or text declaration not at start of entity");
    case XML_ERROR_UNKNOWN_ENCODING:
      return ("unknown encoding");
    case XML_ERROR_INCORRECT_ENCODING:
      return ("encoding specified in XML declaration is incorrect");
    case XML_ERROR_UNCLOSED_CDATA_SECTION:
      return ("unclosed CDATA section");
    case XML_ERROR_EXTERNAL_ENTITY_HANDLING:
      return ("error in processing external entity reference");
    case XML_ERROR_NOT_STANDALONE:
      return ("document is not standalone");
    case XML_ERROR_UNEXPECTED_STATE:
      return ("unexpected parser state - please send a bug report");
    case XML_ERROR_ENTITY_DECLARED_IN_PE:
      return ("entity declared in parameter entity");
    case XML_ERROR_FEATURE_REQUIRES_XML_DTD:
      return ("requested feature requires XML_DTD support in Expat");
    case XML_ERROR_CANT_CHANGE_FEATURE_ONCE_PARSING:
      return ("cannot change setting once parsing has begun");
    /* Added in 1.95.7. */
    case XML_ERROR_UNBOUND_PREFIX:
      return ("unbound prefix");
    /* Added in 1.95.8. */
    case XML_ERROR_UNDECLARING_PREFIX:
      return ("must not undeclare prefix");
    case XML_ERROR_INCOMPLETE_PE:
      return ("incomplete markup in parameter entity");
    case XML_ERROR_XML_DECL:
      return ("XML declaration not well-formed");
    case XML_ERROR_TEXT_DECL:
      return ("text declaration not well-formed");
    case XML_ERROR_PUBLICID:
      return ("illegal character(s) in public id");
    case XML_ERROR_SUSPENDED:
      return ("parser suspended");
    case XML_ERROR_NOT_SUSPENDED:
      return ("parser not suspended");
    case XML_ERROR_ABORTED:
      return ("parsing aborted");
    case XML_ERROR_FINISHED:
      return ("parsing finished");
    case XML_ERROR_SUSPEND_PE:
      return ("cannot suspend in external parameter entity");
    /* Added in 2.0.0. */
    case XML_ERROR_RESERVED_PREFIX_XML:
      return ("reserved prefix (xml) must not be undeclared or bound to another namespace name");
    case XML_ERROR_RESERVED_PREFIX_XMLNS:
      return ("reserved prefix (xmlns) must not be declared or undeclared");
    case XML_ERROR_RESERVED_NAMESPACE_URI:
      return ("prefix must not be bound to one of the reserved namespace names");
    /* Added in 2.2.5. */
    case XML_ERROR_INVALID_ARGUMENT: /* Constant added in 2.2.1, already */
      return ("invalid argument");
      /* Added in 2.3.0. */
    case XML_ERROR_NO_BUFFER:
      return ("a successful prior call to function XML_GetBuffer is required");
    /* Added in 2.4.0. */
    case XML_ERROR_AMPLIFICATION_LIMIT_BREACH:
      return ("limit on input amplification factor (from DTD and entities) breached");
    /* Added in 2.6.4. */
    case XML_ERROR_NOT_STARTED:
      return ("parser not started");
  }
  return nullptr;
}
