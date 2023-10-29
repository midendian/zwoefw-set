/*
 * Crude control program for ZWO EAF focuser motor using hidapi and stuff
 * learned from looking at usbmon/wireshark.
 *
 * Linux:
 *   gcc -o zwoeaf-set zwoeaf-set.c -lhidapi-libusb -Wall -Werror
 * OS X hidapi built from source:
 *   gcc -o zwoeaf-set zwoeaf-set.c -L/.../hidapi/build/src/mac -lhidapi -Wall -Werror
 * OS X hidapi from homebrew:
 *   gcc -o zwoeaf-set zwoeaf-set.c -lhidapi -Wall -Werror
 *
 * Run:
 *   ./zwoeaf-set [<abs pos>|<[-+]rel pos]; echo $?
 * Prints current+max position if no arg given. If movement requested, will
 * continue printing current+target position until exit; if $?=0 current and
 * target should be same. Use last row of output to get current position
 * regardless. May need sudo on Linux.
 *
 * Only tested with my one "new" 5V device.
 *
 * FIXME: would be better to do this in python but the hid/hidapi wrapper
 * distributions seem to be a complete mess of mismatched versions on ubuntu at
 * least.
 *
 * hidapi API docs:
 *   http://hidapi-d.dpldocs.info/hidapi.bindings.html
 * wireshark with usbmon:
 *   https://wiki.wireshark.org/CaptureSetup/USB
 * official ZWO SDK (binary only):
 *   https://www.zwoastro.com/downloads/developers
 *
 *
 *
 * MIT License
 *
 * Copyright (c) 2023 Adam Fritzler <mid@zigamorph.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>
#include <unistd.h>
#include <string.h>

#include <hidapi/hidapi.h>

#define ZWO_USB_VENDOR_ID 0x03c3
#define ZWO_USB_PRODUCT_ID_EAF 0x1f10

/* for requesting feature reports, add one to this and include report ID */
#define ZWO_REPORT_LEN 16

/*
 init:  pos 25000 (0x61a8)
  out  037e5a02030000000000000000000000
   in  017e5a030000000061a8007f7e32ea60
  out  037e5a02030000000000000000000000
   in  017e5a030000000061a8007f7e32ea60
  out  037e5a02030000000000000000000000
   in  017e5a030000000061a8007f7e32ea60

 move: pos 25000 (0x61a8) to 26000 (0x6590)
  out  037e5a02030000000000000000000000
   in  017e5a030000000061a8007fd232ea60
  out  037e5a0301000000659000000002ea60
  out  037e5a02030000000000000000000000
   in  017e5a030100000061d6007fd232ea60   # 25046=0x61d6
  out  037e5a02030000000000000000000000
   in  017e5a030100000061d7007fd232ea60
  out  037e5a02030000000000000000000000
   in  017e5a03010000006258007fd432ea60   # 25176=0x6258
  out  037e5a02030000000000000000000000
   in  017e5a03010000006259007fd432ea60
  out  037e5a02030000000000000000000000
   in  017e5a030100000062da007fd432ea60   # 25306=0x62da
  out  037e5a02030000000000000000000000
...
  out  037e5a02030000000000000000000000
   in  017e5a03000000006590007fd232ea60   # 26000=0x6590
  out  037e5a02030000000000000000000000
   in  017e5a03000000006590007fd232ea60   # 26000=0x6590
 */

int
eaf_set_position(hid_device *devh, uint16_t pos) {
  uint8_t buf[ZWO_REPORT_LEN];

  /* 037e5a0301 0000 00 d6d8 0000 0002 ea60 */
  /* 037e5a0301 0000 00 6590 0000 0002 ea60 */
  memset(buf, 0, sizeof(buf));
  int i = 0;
  buf[i++] = 0x03; // report ID
  buf[i++] = 0x7e;
  buf[i++] = 0x5a;
  buf[i++] = 0x03;
  buf[i++] = 0x01;
  buf[i++] = 0x00;
  buf[i++] = 0x00;
  buf[i++] = 0x00;
  buf[i++] = (pos >> 8) & 0xff;
  buf[i++] = (pos >> 0) & 0xff;
  /* remainder seems unused? */
  buf[13] = 0x02;
  buf[14] = 0xea;
  buf[15] = 0x60;
  int res = hid_send_feature_report(devh, buf, ZWO_REPORT_LEN);
  if (res != ZWO_REPORT_LEN)
    return -1;

  /* no response report for this */
  return 0;
}

int
eaf_get_position(hid_device *devh, uint16_t *posret, uint16_t *posmaxret) {
  uint8_t buf[1+ZWO_REPORT_LEN];

  if (!posret)
    return -1;

  memset(buf, 0, sizeof(buf));
  int i = 0;
  buf[i++] = 0x03; // report ID
  buf[i++] = 0x7e;
  buf[i++] = 0x5a;
  buf[i++] = 0x02;
  buf[i++] = 0x03;
  int res = hid_send_feature_report(devh, buf, ZWO_REPORT_LEN);
  if (res != ZWO_REPORT_LEN)
    return -1;

  memset(buf, 0, sizeof(buf));
  buf[0] = 0x01; // report ID
  res = hid_get_feature_report(devh, buf, 1+ZWO_REPORT_LEN);
  if (res != ZWO_REPORT_LEN)
    return -1;
  /* check assumptions on the bytes seem to be constant... */
  if ( (buf[0] != 0x01) ||
       (buf[1] != 0x7e) || (buf[2] != 0x5a) ||
       (buf[3] != 0x03) ||
       (buf[5] != 0x00) || (buf[6] != 0x00) || (buf[7] != 0x00) ||
       (buf[10] != 0x00) ||
       (buf[14] != 0xea) || (buf[15] != 0x60) ) {
    fprintf(stderr, "unexpected values in position report: ");
    for (i = 0; i < ZWO_REPORT_LEN; i++)
      fprintf(stderr, " %02x", buf[i]);
    fprintf(stderr, "\n");
  }
  uint8_t status = buf[4]; /* 1=moving, 0=stable ? */
  uint16_t position = (buf[8] << 8) | buf[9];
  uint8_t status2 = buf[11]; /* no idea. */
  uint8_t status3 = buf[12]; /* no idea. */
  /* buf[13] is garbage from whatever was in the buffer */
  printf("position report: status=%d, status2=0x%02x, status3=0x%02x, position=%d\n",
         status, status2, status3, position);

  *posret = position;
  if (posmaxret)
    *posmaxret = (buf[14] << 8) | buf[15];
  if (status != 0)
    return 1;
  return 0;
}

int
main(int argc, char* argv[]) {

  long int targetpos = -1;
  bool targetrel = false;
  const char *targetrelsign = NULL;
  if (argc > 1) {
    const char *str = argv[1];
    if ( (str[0] == '-') || (str[0] == '+') ) {
      targetrel = true;
      targetrelsign = str;
      str++;
    }
    targetpos = strtol(str, NULL, 10);
    if ( (targetpos < 0) || (targetpos > 0xffff) ) { /* real max down below */
      fprintf(stderr, "invalid position requested\n");
      goto errexitlast;
    }
  }

  int res = hid_init();
  if (res != 0) {
    fprintf(stderr, "hid_init failed\n");
    goto errexitlast;
  }

  hid_device *handle = \
    hid_open(ZWO_USB_VENDOR_ID, ZWO_USB_PRODUCT_ID_EAF, NULL);
  if (!handle) {
    fprintf(stderr, "unable to open device\n");
    goto errexit;
  }

  /* this is in a loop in case it's moving when we start. */
  uint16_t pos = 0, posmax = 0;
  for (;;) {
    res = eaf_get_position(handle, &pos, &posmax);
    if (res == -1) {
      fprintf(stderr, "unrecoverable error, needs physical reset\n");
      goto errexit;
    } else if (res == 0) break;
    usleep(500*1000);
  }
  printf("current pos = %d (max %d)\n", pos, posmax);

  if (targetpos != -1) {
    if (targetrel)
      targetpos = pos + (targetpos * (*targetrelsign == '-' ? -1 : 1));
    fprintf(stderr, "requesting target %ld\n", targetpos);
    if ( (targetpos < 0) || (targetpos > posmax) ) {
      fprintf(stderr, "invalid target %ld\n", targetpos);
      goto errexit;
    }
    if (eaf_set_position(handle, (uint16_t)targetpos) != 0)
      goto errexit;

    while (pos != targetpos) {
      res = eaf_get_position(handle, &pos, NULL);
      if (res == -1) {
        fprintf(stderr, "unrecoverable error, needs physical reset\n");
        goto errexit;
      }
      printf("current pos = %d (target %ld)\n", pos, targetpos);
      if ( (res == 0) && (pos == targetpos) ) break;
      usleep(500*1000);
    }
  }

  hid_close(handle);
  hid_exit();
  exit(0);

errexit:
  if (handle)
    hid_close(handle);
  hid_exit();
errexitlast:
  exit(2);

  return 0; /* not reached */
}
