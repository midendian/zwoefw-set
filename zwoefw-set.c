/*
 * Crude control program for ZWO EFW filter wheel using hidapi and stuff
 * learned from looking at usbmon/wireshark.
 *
 * Takes a slot number (1..7) as the only arg and will exit with clean status
 * if it successfully made it there. Otherwise exits with error code. The
 * intention is that you'd only call this and check the error code; stdout is
 * useless and stderr only useful for debugging.
 *
 * Linux:
 *   gcc -o zwoefw-set zwoefw-set.c -lhidapi-libusb -Wall -Werror
 * OS X hidapi built from source:
 *   gcc -o zwoefw-set zwoefw-set.c -L/.../hidapi/build/src/mac -lhidapi -Wall -Werror
 * OS X hidapi from homebrew:
 *   gcc -o zwoefw-set zwoefw-set.c -lhidapi -Wall -Werror
 *
 * Run:
 *   ./zwoefw-set [<slot num>]; echo $?
 * Moves to slot 1 if no arg given. May need sudo on Linux.
 *
 * Only tested with my one 7-slot device, obviously needs some work for other
 * variants and possibly other copies of the same variant.
 *
 * FIXME: for some reason I can only get the wheel to go forward, while it's
 * clearly capable of going in reverse to speed things up. I've compared the
 * commands pretty closely and cannot figure out what this code does
 * differently. Unfortunately this is more than just inefficiency as the
 * wheel's micro times out and requires a hard reset going from slot 1 to 7
 * directly. This program avoids that problem by only moving one step at a time
 * but this is extremely slow (about 15 seconds) since it also stops and does
 * the fine alignment on each slot.
 *
 * FIXME: would be better to do this in python but the hid/hidapi wrapper
 * distributions seem to be a complete mess of mismatched versions on ubuntu at
 * least.
 *
 * hidapi API docs:
 *   http://hidapi-d.dpldocs.info/hidapi.bindings.html
 * wireshark with usbmon:
 *   https://wiki.wireshark.org/CaptureSetup/USB
 * official ZWO EFW SDK (binary only):
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
#include <stdint.h>
#include <wchar.h>
#include <unistd.h>
#include <string.h>

#include <hidapi/hidapi.h>

#define ZWO_USB_VENDOR_ID 0x03c3
#define ZWO_USB_PRODUCT_ID_EFW 0x1f01

/* for requesting feature reports, add one to this and include report ID */
#define ZWO_REPORT_LEN 16

/*
   bmRequestType 0xa1 = get report
   bmRequestType 0x21 = set report
   wValue = [request type, report id]

   out 03 7e5a 02040000000000000000000000
    in 01 7e5a 04030009004546572d532d3000 ... EFW-S-0\0
   out 03 7e5a 02010000000000000000000000
    in 01 7e5a 01010001010107000000003000
   out 03 7e5a 02010000000000000000000000   get position?

   for zwo efw:
     wValue[0] (request type) is always 0x03 (feature)
     wValue[1] (report ID) = 0x03 when bmRequestType==0x21 (bRequest=9)
     wValue[1] (report ID) = 0x01 when bmRequestType==0xa1 (bRequest=1)
     wLength must be 17 when bmRequestType==0xa1 (bRequest=1)
       other request lengths will fail to produce expected result
     wLength must be 16 when bmRequestType==0x21 (bRequest=9)
     first two data bytes must be [0x7e, 0x5a] "~Z"
 */

int
efw_get_info(hid_device *devh) {
  uint8_t buf[1+ZWO_REPORT_LEN];

  memset(buf, 0, sizeof(buf));
  int i = 0;
  buf[i++] = 0x03; // report ID
  buf[i++] = 0x7e;
  buf[i++] = 0x5a;
  buf[i++] = 0x02;
  buf[i++] = 0x04;
  int res = hid_send_feature_report(devh, buf, ZWO_REPORT_LEN);
  if (res != ZWO_REPORT_LEN)
    return -1;

  memset(buf, 0, sizeof(buf));
  buf[0] = 0x01; // report ID
  /* if you request more than ZWO_REPORT_LEN it will send gibberish... */
  res = hid_get_feature_report(devh, buf, 1+ZWO_REPORT_LEN);
  if (res != ZWO_REPORT_LEN)
    return -1;

  static const uint8_t expected[ZWO_REPORT_LEN] = {
    0x01, 0x7e, 0x5a, 0x04, 0x03, 0x00, 0x09, 0x00,
    0x45, 0x46, 0x57, 0x2d, 0x53, 0x2d, 0x30, 0x00,
  };
  if (memcmp(expected, buf, ZWO_REPORT_LEN) != 0) {
    fprintf(stderr, "unexpected values in info report: ");
    for (i = 0; i < ZWO_REPORT_LEN; i++)
      fprintf(stderr, " %02x", buf[i]);
    fprintf(stderr, "\n");
  }
  return 0; /* TODO return info string */
}

int
efw_set_position(hid_device *devh, uint8_t slot) {
  uint8_t buf[ZWO_REPORT_LEN];

  if (slot < 1 || slot > 7)
    return -1;

  memset(buf, 0, sizeof(buf));
  int i = 0;
  buf[i++] = 0x03; // report ID
  buf[i++] = 0x7e;
  buf[i++] = 0x5a;
  buf[i++] = 0x01;
  buf[i++] = 0x02;
  buf[i++] = slot; /* first filter is 1 not 0 */
  int res = hid_send_feature_report(devh, buf, ZWO_REPORT_LEN);
  if (res != ZWO_REPORT_LEN)
    return -1;

  /* no response report for this */
  return 0;
}

int
efw_get_position(hid_device *devh, uint8_t *slotret) {
  uint8_t buf[1+ZWO_REPORT_LEN];

  if (!slotret)
    return -1;

  memset(buf, 0, sizeof(buf));
  int i = 0;
  buf[i++] = 0x03; // report ID
  buf[i++] = 0x7e;
  buf[i++] = 0x5a;
  buf[i++] = 0x02;
  buf[i++] = 0x01;
  int res = hid_send_feature_report(devh, buf, ZWO_REPORT_LEN);
  if (res != ZWO_REPORT_LEN)
    return -1;

  /*
     examples:
      01 7e 5a 01 04 00 03 02 03 07 00 00 00 00 30 00
      01 7e 5a 01 01 00 03 03 03 07 00 00 00 00 30 00
      01 7e 5a 01 06 0c 07 06 07 07 00 00 00 00 30 00
      01 7e 5a 01 06 0c 07 06 07 07 00 00 00 00 30 00

     suspect the last six bytes are completely unused and just contain values
     from whatever last request actually used that much of the buffer on the
     wheel side, but it doesn't matter.
   */
  memset(buf, 0, sizeof(buf));
  buf[0] = 0x01; // report ID
  res = hid_get_feature_report(devh, buf, 1+ZWO_REPORT_LEN);
  if (res != ZWO_REPORT_LEN)
    return -1;
  /* check assumptions on the bytes seem to be constant... */
  if ( (buf[0] != 0x01) ||
       (buf[1] != 0x7e) || (buf[2] != 0x5a) ||
       (buf[3] != 0x01) ||
       (buf[10] != 0x00) || (buf[11] != 0x00) ||
       (buf[12] != 0x00) || (buf[13] != 0x00) ||
       (buf[14] != 0x30) || (buf[15] != 0x00) ) {
    fprintf(stderr, "unexpected values in position report: ");
    for (i = 0; i < ZWO_REPORT_LEN; i++)
      fprintf(stderr, " %02x", buf[i]);
    fprintf(stderr, "\n");
  }
  uint8_t status = buf[4]; /* 4=moving, 1=stable ? */
  uint8_t errcode = buf[5];
  /* just guessing on these... */
  uint8_t slot_current = buf[6];
  uint8_t slot_max = buf[9];
  printf("position report: status=%d, [%d, %d, %d], max=%d\n",
         status, buf[6], buf[7], buf[8], slot_max);

  if ( (buf[6] == buf[7]) && (buf[7] == buf[8]) && (status == 1) ) {
    *slotret = slot_current;
    return 0;
  }
  if ( (status == 6) || (errcode != 0) ) {
    /* seems to be unrecoverable electronically, wheel needs a hard reset */
    return -1;
  }
  return 1; /* caller should wait it out */
}

int
main(int argc, char* argv[]) {

  uint8_t targetslot = 0;
  if (argc > 1) {
    long int argint = strtol(argv[1], NULL, 10);
    if ( (argint < 1) || (argint > 7) ) {
      fprintf(stderr, "invalid filter slot requested\n");
      goto errexitlast;
    }
    targetslot = (uint8_t)argint;
  }

  int res = hid_init();
  if (res != 0) {
    fprintf(stderr, "hid_init failed\n");
    goto errexitlast;
  }

  hid_device *handle = hid_open(ZWO_USB_VENDOR_ID, ZWO_USB_PRODUCT_ID_EFW, NULL);
  if (!handle) {
    fprintf(stderr, "unable to open device\n");
    goto errexit;
  }

  int i;

#ifndef __APPLE__ /* this segfaults on OS X, not interesting enough to debug */
  wchar_t wstr[255];
  res = hid_get_manufacturer_string(handle, wstr, sizeof(wstr));
  if (res != 0) goto errexit;
  printf("Manufacturer String: %ls\n", wstr);
  res = hid_get_product_string(handle, wstr, sizeof(wstr));
  if (res != 0) goto errexit;
  printf("Product String: %ls\n", wstr);
#endif

  if (efw_get_info(handle) != 0)
    goto errexit;

  uint8_t slot;
  for (;;) {
    res = efw_get_position(handle, &slot);
    if (res == -1) {
      fprintf(stderr, "unrecoverable wheel error, needs physical reset\n");
      goto errexit;
    } else if (res == 0) break;
    usleep(500*1000);
  }
  if (targetslot == 0)
    targetslot = slot; /* no change requested */

  while (slot != targetslot) {

    uint8_t nextslot = ((slot - 1 + 1) % 7) + 1;
    printf("request slot %d\n", nextslot);
    if (efw_set_position(handle, nextslot) != 0)
      goto errexit;

    for (i = 0; i < 100; i++) {
      res = efw_get_position(handle, &slot);
      /* it takes a moment for it to process the slot change, so only stop
       * polling if we've made it even if not currently moving.
       */
      if (res == -1) {
        fprintf(stderr, "unrecoverable wheel error, needs physical reset\n");
        goto errexit;
      }
      if ( (res == 0) && (slot == nextslot) ) break;
      usleep(500*1000);
    }
    printf("current slot = %d\n", slot);
  }

  printf("final slot = %d\n", slot);

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
