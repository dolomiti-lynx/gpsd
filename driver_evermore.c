/*
 *
 * This is the gpsd driver for EverMore GPSes operating in binary mode.
 * About the only thing this gives us that NMEA won't is TDOP.
 * But we'll get atomic position reports from it, which is good.
 *
 * The vendor site is <http://www.emt.com.tw>.
 *
 * This driver was written by Petr Slansky based on a framework by Eric S. 
 * Raymond.  The following remarks are by Petr Slansky.
 *
 * Snooping on the serial the communication between a Windows program and
 * an Evermore chipset reveals some messages not described in the vendor
 * documentation (Issue C of Aug 2002):
 *
 * 10 02 06 84 00 00 00 84 10 03	switch to binary mode (84 00 00 00)
 * 10 02 06 84 01 00 00 85 10 03	switch to NMEA mode (84 01 00 00)
 *
 * 10 02 06 89 01 00 00 8a 10 03        set baud rate 4800
 * 10 02 06 89 01 01 00 8b 10 03        set baud rate 9600
 * 10 02 06 89 01 02 00 8c 10 03        set baud rate 19200
 * 10 02 06 89 01 03 00 8d 10 03        set baud rate 38400
 *
 * 10 02 06 8D 00 01 00 8E 10 03        switch to datum ID 001 (WGS-84)
 * 10 02 06 8D 00 D8 00 65 10 03        switch to datum ID 217 (WGS-72)
 *
 * These don't entail a reset of GPS as the 0x80 message does.
 * 
 * 10 02 04 38 85 bd 10 03     answer from GPS to 0x85 message; ACK message
 * 10 02 04 38 8d c5 10 03     answer from GPS to 0x8d message; ACK message
 * 10 02 04 38 8e c6 10 03     answer from GPS to 0x8e message; ACK message
 * 10 02 04 38 8f c7 10 03     answer from GPS to 0x8f message; ACK message
 *
 * The chip sometimes sends vendor extension messages with the prefix
 * $PEMT,100. After restart, it sends a $PEMT,100 message describing the
 * chip's configuration. Here is a sample:
 *
 * $PEMT,100,05.42g,100303,180,05,1,20,15,08,0,0,2,1*5A
 * 100 - message type
 * 05.42g - firmware version
 * 100303 - date of firmware release DDMMYY
 * 180 -  datum ID; 001 is WGS-84
 * 05 - default elevation mask; see message 0x86
 * 1 - default DOP select (1 is auto DOP mask); see message 0x87
 * 20 - default GDOP; see message 0x87
 * 15 - default PDOP
 * 08 - default HDOP
 * 0 - Normal mode, without 1PPS
 * 0 - default position pinning control (0 disable, 1 enable)
 * 2 - altitude hold mode (0 disable, 1 always, 2 auto)
 * 1 - 2/1 satellite nav mode (0,1,2,3,4)
 *          0 disable 2/1 sat nav mode
 *          1 hold direction (2 sat)
 *          2 clock hold only (2 sat)
 *          3 direction hold then clock hold (1 sat)
 *          4 clock hold then direction hold (1 sat)
 * 
 * Message $PEMT,100 could be forced with message 0x85 (restart):
 * 10 02 12 85 00 00 00 00 00 01 01 00 00 00 00 00 00 00 00 87 10 03
 * 0x85 ID, Restart
 * 0x00 restart mode (0 default, 1 hot, 2 warm, 3 cold, 4 test)
 * 0x00 test start search PRN (1-32)
 * 0x00 UTC second (0-59)
 * 0x00 UTC Minute (0-59)
 * 0x00 UTC Hour (0-23)
 * 0x01 UTC Day (1-31)
 * 0x01 UTC Month (1-12)
 * 0x0000 UTC year (1980+x, uint16)
 * 0x0000 Latitude WGS-84 (+/-900, 1/10 degree, + for N, int16)
 * 0x0000 Longtitude WGS-84 (+/-1800, 1/10 degree, + for E, int16)
 * 0x0000 Altitude WGS-84 (-1000..+18000, meters, int16)
 * 0x87 CRC
 * 
 * With message 0x8e it is possible to define how often each NMEA
 * message is sent (0-255 seconds). It is possible with message 0x8e
 * to activate PEMT,101 messages that have information about time,
 * position, velocity and HDOP.
 * 
 * $PEMT,101,1,02,00.0,300906190446,5002.5062,N,01427.6166,E,00259,000,0000*27
 * $PEMT,101,2,06,02.1,300906185730,5002.7546,N,01426.9524,E,00323,020,0011*26
 * 101 - message type, Compact Navigation Solution
 * 2 - position status (1,2,3,4,5,6)
 *      (1 invalid, 2 2D fix, 3 3D fix, 4 2D with DIFF, 5 3D with DIFF,
 *       6 2/1 sat degrade mode) 
 * 06 - number of used satelites
 * 02.1 - DOP (00.0 no fix, HDOP 2D fix, PDOP 3D fix)
 * 300906185730 - date and time, UTC ddmmyyHHMMSS (30/09/2006 18:57:30)
 * 5002.7546,N - Latitude (degree)
 * 01426.9524,E - Longitude (degree)
 * 00323 - Altitude (323 metres)
 * 020 - heading (20 degrees from true north)
 * 0011 - speed over ground (11 metres per second); documentation says km per h
 * 
 * This is an exampe of an 0x8e message that activates all NMEA sentences 
 * with 1s period:
 * 10 02 12 8E 7F 01 01 01 01 01 01 01 01 00 00 00 00 00 00 15 10 03
 * 
 * There is a way to probe for this chipset. When binary message 0x81 is sent:
 * 10 02 04 81 13 94 10 03
 *
 * EverMore will reply with message like this:
 * *10 *02 *0D *20 E1 00 00 *00 0A 00 1E 00 32 00 5B *10 *03
 * bytes marked with * are fixed
 * Message in reply is information about logging configuration of GPS
 *
 * Another way to detect the EverMore chipset is to send one of the messages 
 * 0x85, 0x8d, 0x8e or 0x8f and check for a reply.
 * The reply message from an EverMore GPS will look like this:
 * *10 *02 *04 *38 8d c5 *10 *03
 * 8d indicates that message 0x8d was sent;
 * c5 is EverMore checksum, other bytes are fixed
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details. 
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#ifndef S_SPLINT_S
#include <unistd.h>
#endif /* S_SPLINT_S */
#include <time.h>
#include <stdio.h>

#include "gpsd.h"
#if defined(EVERMORE_ENABLE) && defined(BINARY_ENABLE)

#define GET_ORIGIN 1
#define PUT_ORIGIN 0
#include "bits.h"

#define EVERMORE_CHANNELS	12

/*@ +charint @*/
gps_mask_t evermore_parse(struct gps_device_t *session, unsigned char *buf, size_t len)
{
    unsigned char buf2[MAX_PACKET_LENGTH], *cp, *tp;
    size_t i, datalen;
    unsigned int type, used, visible, satcnt;
    double version;
    gps_mask_t mask = 0;

    if (len == 0)
	return 0;

    /* time to unstuff it and discard the header and footer */
    cp = buf + 2;
    tp = buf2;
    if (*cp == 0x10) cp++;
    datalen = (size_t)*cp++;

    gpsd_report(LOG_RAW, "raw EverMore packet type 0x%02x, length %zd: %s\n",
	*cp, len, gpsd_hexdump_wrapper(buf, len, LOG_RAW));

    datalen -= 2;

    /*@ -usedef @*/
    for (i = 0; i < (size_t)datalen; i++) {
	*tp = *cp++;
	if (*tp == 0x10) cp++;
	tp++;
    }

    type = (unsigned char)getub(buf2, 1);
    /*@ +usedef @*/

    /*@ -usedef -compdef @*/
    gpsd_report(LOG_RAW, "EverMore packet type 0x%02x, length %zd: %s\n",
	type, datalen, gpsd_hexdump_wrapper(buf2, datalen, LOG_RAW));
    /*@ +usedef +compdef @*/

    (void)snprintf(session->gpsdata.tag, sizeof(session->gpsdata.tag),
		   "EID%u", type);

    session->cycle_end_reliable = true;

    switch (type)
    {
    case 0x02:	/* Navigation Data Output */
	/*@ ignore @*//*@ splint is confused @*/
	session->newdata.time =
	    gpstime_to_unix((int)getleuw(buf2, 2), getleul(buf2, 4)*0.01) - session->context->leap_seconds;
	/*@ end @*/
	ecef_to_wgs84fix(&session->newdata, &session->gpsdata.separation,
			 getlesl(buf2, 8)*1.0, getlesl(buf2, 12)*1.0, getlesl(buf2, 16)*1.0,
			 getlesw(buf2, 20)/10.0, getlesw(buf2, 22)/10.0, getlesw(buf2, 24)/10.0);
	used = (unsigned char)getub(buf2, 26) & 0x0f;
	//visible = (getub(buf2, 26) & 0xf0) >> 4;
	version = (uint)getleuw(buf2, 27)/100.0;
	/* that's all the information in this packet */
	if (used < 3)
	    session->newdata.mode = MODE_NO_FIX;
	else if (used == 3)
	    session->newdata.mode = MODE_2D;
	else {
	    session->newdata.mode = MODE_3D;
	    mask |= ALTITUDE_IS | CLIMB_IS;
	}
	mask |= TIME_IS | LATLON_IS | TRACK_IS | SPEED_IS | MODE_IS;
	if (session->subtype[0] == '\0') {
	    (void)snprintf(session->subtype, sizeof(session->subtype), 
		       "%3.2f", version);
	    mask |= DEVICEID_IS;
	}
	gpsd_report(LOG_DATA, "NDO 0x02: time=%.2f, lat=%.2f lon=%.2f alt=%.2f speed=%.2f track=%.2f climb=%.2f mode=%d subtype='%s' mask=%s\n",
		    session->newdata.time,
		    session->newdata.latitude,
		    session->newdata.longitude,
		    session->newdata.altitude,
		    session->newdata.speed,
		    session->newdata.track,
		    session->newdata.climb,
		    session->newdata.mode,
		    session->gpsdata.dev.subtype,
		    gpsd_maskdump(mask));
	return mask | CLEAR_IS | REPORT_IS;

    case 0x04:	/* DOP Data Output */
	/*@ ignore @*//*@ splint is confused @*/
	session->newdata.time =
	    gpstime_to_unix((int)getleuw(buf2, 2), getleul(buf2, 4)*0.01) - session->context->leap_seconds;
	/*@ end @*/
	clear_dop(&session->gpsdata.dop);
	session->gpsdata.dop.gdop = (double)getub(buf2, 8)*0.1;
	session->gpsdata.dop.pdop = (double)getub(buf2, 9)*0.1;
	session->gpsdata.dop.hdop = (double)getub(buf2, 10)*0.1;
	session->gpsdata.dop.vdop = (double)getub(buf2, 11)*0.1;
	session->gpsdata.dop.tdop = (double)getub(buf2, 12)*0.1;
	switch (getub(buf2, 13)) {
	case 0:	/* no position fix */
	case 1:	/* manual calls this "1D navigation" */
	    session->gpsdata.status = STATUS_NO_FIX;
	    session->newdata.mode = MODE_NO_FIX;
	    break;
	case 2:	/* 2D navigation */
	    session->gpsdata.status = STATUS_FIX;
	    session->newdata.mode = MODE_2D;
	    break;
	case 3:	/* 3D navigation */
	    session->gpsdata.status = STATUS_FIX;
	    session->newdata.mode = MODE_3D;
	    break;
	case 4:	/* 3D navigation with DGPS */
	    session->gpsdata.status = STATUS_DGPS_FIX;
	    session->newdata.mode = MODE_3D;
	    break;
	}
	/* that's all the information in this packet */
	mask = TIME_IS | DOP_IS | MODE_IS | STATUS_IS;
	gpsd_report(LOG_DATA, "DDO 0x04: gdop=%.2f pdop=%.2f hdop=%.2f vdop=%.2f tdop=%.2f mode=%d, status=%d mask={TIME| DOP|MODE|STATUS}\n", 
		    session->gpsdata.dop.gdop,
		    session->gpsdata.dop.pdop,
		    session->gpsdata.dop.hdop,
		    session->gpsdata.dop.vdop,
		    session->gpsdata.dop.tdop,
		    session->newdata.mode,
		    session->gpsdata.status);
	return mask;

    case 0x06:	/* Channel Status Output */
	/*@ ignore @*//*@ splint is confused @*/
	session->gpsdata.skyview_time =
	    gpstime_to_unix((int)getleuw(buf2, 2), getleul(buf2, 4)*0.01) - session->context->leap_seconds;
	/*@ end @*/
	session->gpsdata.satellites_visible = (int)getub(buf2, 8);
	session->gpsdata.satellites_used = 0;
	memset(session->gpsdata.used, 0, sizeof(session->gpsdata.used));
	if (session->gpsdata.satellites_visible > 12) {
		gpsd_report(LOG_WARN, "Warning: EverMore packet has information about %d satellites!\n",
				session->gpsdata.satellites_visible);
	}
	if (session->gpsdata.satellites_visible > EVERMORE_CHANNELS) 
	    session->gpsdata.satellites_visible = EVERMORE_CHANNELS;
	satcnt = 0;
	for (i = 0; i < (size_t)session->gpsdata.satellites_visible; i++) {
	    int prn;
	    // channel = getub(buf2, 7*i+7+2)
	    prn = (int)getub(buf2, 7*i+7+3);
	    if (prn == 0) continue;  /* satellite record is not valid */
	    session->gpsdata.PRN[satcnt] = prn;
	    session->gpsdata.azimuth[satcnt] = (int)getleuw(buf2, 7*i+7+4);
	    session->gpsdata.elevation[satcnt] = (int)getub(buf2, 7*i+7+6);
	    session->gpsdata.ss[satcnt] = (float)getub(buf2, 7*i+7+7);
	    /*
	     * Status bits at offset 8:
	     * bit0 = 1 satellite acquired
	     * bit1 = 1 code-tracking loop locked
	     * bit2 = 1 carrier-tracking loop locked
	     * bit3 = 1 data-bit synchronization done
	     * bit4 = 1 frame synchronization done
	     * bit5 = 1 ephemeris data collected
	     * bit6 = 1 used for position fix
	     */
	    if (getub(buf2, 7*i+7+8) & 0x40) {
		session->gpsdata.used[session->gpsdata.satellites_used++]=prn;
	    }

	    satcnt++;
	}
	session->gpsdata.satellites_visible = (int)satcnt;
	/* that's all the information in this packet */
	mask = SATELLITE_IS | USED_IS;
	gpsd_report(LOG_DATA, "CSO 0x06: time=%.2f used=%d visible=%d mask={TIME|SATELLITE|USED}\n",
		    session->newdata.time,
		    session->gpsdata.satellites_used,
		    session->gpsdata.satellites_visible);
	return mask;

    case 0x08:	/* Measurement Data Output */
	/* clock offset is a manufacturer diagnostic */
	/* (int)getleuw(buf2, 8);  clock offset, 29000..29850 ?? */
	/*@ ignore @*//*@ splint is confused @*/
	session->newdata.time = 
	    gpstime_to_unix((int)getleuw(buf2, 2), getleul(buf2, 4)*0.01) - session->context->leap_seconds;
	/*@ end @*/
	visible = (unsigned char)getub(buf2, 10);
	/* FIXME: read full statellite status for each channel */
	/* we can get pseudo range (m), delta-range (m/s), doppler (Hz) and status for each channel */
	/* gpsd_report(LOG_PROG, "MDO 0x04: visible=%d\n", visible); */
	gpsd_report(LOG_DATA, "MDO 0x04: time=%.2f mask={TIME}\n",
		    session->newdata.time);
	return TIME_IS;

    case 0x20:	/* LogConfig Info, could be used as a probe for EverMore GPS */
	gpsd_report(LOG_IO, "LogConfig EverMore packet, length %zd: %s\n",
	    datalen, gpsd_hexdump_wrapper(buf2, datalen, LOG_IO));
	return ONLINE_IS;

    case 0x22:	/* LogData */
	gpsd_report(LOG_IO, "LogData EverMore packet, length %zd: %s\n",
	    datalen, gpsd_hexdump_wrapper(buf2, datalen, LOG_IO));
	return ONLINE_IS;

    case 0x38:	/* ACK */
	gpsd_report(LOG_PROG, "EverMore command %02X ACK\n", getub(buf2, 2));
	return ONLINE_IS;

    default:
	gpsd_report(LOG_WARN,
	    "unknown EverMore packet EID 0x%02x, length %zd: %s\n", buf2[0],
	    datalen, gpsd_hexdump_wrapper(buf2, datalen, LOG_IO));
	return 0;
    }
}
/*@ -charint @*/

static gps_mask_t evermore_parse_input(struct gps_device_t *session)
{
    gps_mask_t st;

    if (session->packet.type == EVERMORE_PACKET) {
	st = evermore_parse(session, session->packet.outbuffer, session->packet.outbuflen);
	return st;
#ifdef NMEA_ENABLE
    } else if (session->packet.type == NMEA_PACKET) {
	st = nmea_parse((char *)session->packet.outbuffer, session);
	return st;
#endif /* NMEA_ENABLE */
    } else
	return 0;
}

/*@ +charint -usedef -compdef @*/
static ssize_t evermore_control_send(struct gps_device_t *session, char *buf, size_t len)
{
   unsigned int       crc;
   size_t    i;
   char *cp;

   /*@ +charint +ignoresigns @*/
   /* prepare a DLE-stuffed copy of the message */
   cp = session->msgbuf;
   *cp++ = 0x10;  /* message starts with DLE STX */
   *cp++ = 0x02;

   session->msgbuflen = (size_t)(len + 2);   /* len < 254 !! */
   *cp++ = (char)session->msgbuflen;   /* message length */
   if (session->msgbuflen == 0x10) *cp++ = 0x10;
   
   /* payload */
   crc = 0;
   for (i = 0; i < len; i++) {
       *cp++ = buf[i];
       if (buf[i] == 0x10) 
	   *cp++ = 0x10;
       crc += buf[i];
   }

   crc &= 0xff;

   /* enter CRC after payload */
   *cp++ = crc;  
   if (crc == 0x10)
       *cp++ = 0x10;

   *cp++ = 0x10;   /* message ends with DLE ETX */
   *cp++ = 0x03;

   session->msgbuflen = (size_t)(cp - session->msgbuf);
   /*@ -charint -ignoresigns @*/

   return gpsd_write(session, session->msgbuf, session->msgbuflen);
}
/*@ -charint +usedef +compdef @*/

static bool evermore_protocol(struct gps_device_t *session, int protocol)
{
    /*@ +charint */
    char tmp8;
    char evrm_protocol_config[] = {
	(char)0x84,    /* 0: msg ID, Protocol Configuration */
	(char)0x00,    /* 1: mode; EverMore binary(0), NMEA(1) */
	(char)0x00,    /* 2: reserved */
	(char)0x00,    /* 3: reserved */
    };
    /*@ -charint */
    gpsd_report(LOG_PROG, "evermore_protocol(%d)\n", protocol);
    /*@i1@*/tmp8 = (protocol != 0) ? 1 : 0;   /* NMEA : binary */
    evrm_protocol_config[1] = tmp8;
    return (evermore_control_send(session, evrm_protocol_config, sizeof(evrm_protocol_config)) != -1);
}

static bool evermore_nmea_config(struct gps_device_t *session, int mode)
/* mode = 0 : EverMore default */
/* mode = 1 : gpsd best */
/* mode = 2 : EverMore search, activate PEMT101 message */
{
    unsigned char tmp8;
    /*@ +charint */
    unsigned char evrm_nmeaout_config[] = {
	    0x8e,	/*  0: msg ID, NMEA Message Control */
	    0xff,       /*  1: NMEA sentence bitmask, GGA(0), GLL(1), GSA(2), GSV(3), ... */
	    0x01,       /*  2: nmea checksum no(0), yes(1) */
	    1,          /*  3: GPGGA, interval 0-255s */
	    0,          /*  4: GPGLL, interval 0-255s */
	    1,          /*  5: GPGSA, interval 0-255s */
	    1,          /*  6: GPGSV, interval 0-255s */
	    1,          /*  7: GPRMC, interval 0-255s */
	    0,          /*  8: GPVTG, interval 0-255s */
	    0,          /*  9: PEMT,101, interval 0-255s */
	    0, 0, 0, 0, 0, 0, /* 10-15: reserved */
    };
    /*@ -charint */
    gpsd_report(LOG_PROG, "evermore_nmea_config(%d)\n", mode);
    /*@i1@*/tmp8 = (mode == 1) ? 5 : 1;   /* NMEA GPGSV, gpsd  */
    evrm_nmeaout_config[6] = tmp8;           /* GPGSV, 1s or 5s */
    /*@i1@*/tmp8 = (mode == 2) ? 1 : 0;   /* NMEA PEMT101 */
    evrm_nmeaout_config[9] = tmp8;           /* PEMT101, 1s or 0s */
    return (evermore_control_send(session, (char *)evrm_nmeaout_config,
				  sizeof(evrm_nmeaout_config)) != -1);
}

static void evermore_mode(struct gps_device_t *session, int mode)
{
    gpsd_report(LOG_PROG, "evermore_mode(%d), %d\n", mode, session->back_to_nmea ? 1 : 0);
    if (mode == MODE_NMEA) {
        /* NMEA */
        (void) evermore_protocol(session, 1);
        session->gpsdata.dev.driver_mode = MODE_NMEA;
        (void) evermore_nmea_config(session, 1); /* configure NMEA messages for gpsd */
    } else {
        /* binary */
        (void) evermore_protocol(session, 0);
        session->back_to_nmea = false;
        session->gpsdata.dev.driver_mode = MODE_BINARY;
    }
}

static void evermore_event_hook(struct gps_device_t *session, event_t event)
{
    /*
     * FIXME: It might not be necessary to call this on reactivate.
     * Experiment to see if the holds its settings through a close.
     */
    if (event == event_identified || event == event_reactivate) {
        if (session->packet.type == NMEA_PACKET) {
	    (void) evermore_nmea_config(session, 1); /* configure NMEA messages for gpsd (GPGSV every 5s) */
        } 
        (void) evermore_mode(session, 1); /* switch GPS to binary mode */
        session->back_to_nmea = true;
    } else if (event == event_deactivate) {
	(void) evermore_nmea_config(session, 0); /* configure NMEA messages to default */
    }
}

#ifdef ALLOW_RECONFIGURE
static bool evermore_speed(struct gps_device_t *session, 
			   speed_t speed, char parity, int stopbits)
{
    /*@ -type @*/
    gpsd_report(LOG_PROG, "evermore_speed(%u%c%d)\n", speed, parity, stopbits);
    /* parity and stopbit switching aren't available on this chip */
    if (parity!=session->gpsdata.dev.parity || stopbits!=(int)session->gpsdata.dev.parity) {
	return false;
    } else {
	unsigned char tmp8;
	unsigned char msg[] = {
	    0x89,          /*  0: msg ID, Serial Port Configuration */
	    0x01,          /*  1: bit 0 cfg for main serial, bit 1 cfg for DGPS port */
	    0x00,          /*  2: baud rate for main serial; 4800(0), 9600(1), 19200(2), 38400(3) */
	    0x00,          /*  3: baud rate for DGPS serial port; 4800(0), 9600(1), etc */
	};
	switch (speed) {
	case 4800:  tmp8 = 0; break;
	case 9600:  tmp8 = 1; break;
	case 19200: tmp8 = 2; break;
	case 38400: tmp8 = 3; break;
	default: return false;
	}
	msg[2] = tmp8;
	return (evermore_control_send(session, (char *)msg, sizeof(msg)) != -1);
    }
    /*@ +type @*/
}

static bool evermore_rate_switcher(struct gps_device_t *session, double rate)
/* change the sample rate of the GPS */
{
    /*@ +charint @*/
    unsigned char evrm_rate_config[] = {
	    0x84,    /* 1: msg ID, Operating Mode Configuration */
	    0x02,    /* 2: normal mode with 1PPS */
	    0x00,    /* 3: navigation update rate */
	    0x00,    /* 4: RF/GPSBBP On Time */
    };

    if (rate < 1 || rate > 10) {
	gpsd_report(LOG_ERROR, "valid rate range is 1-10.\n");
	return false;
    } else {
	evrm_rate_config[2] = (unsigned char)trunc(rate);
	return (evermore_control_send(session, (char *)evrm_rate_config,
				      sizeof(evrm_rate_config)) != -1);
    }
    /*@ -charint @*/
}
#endif /* ALLOW_RECONFIGURE */


/* this is everything we export */
/* *INDENT-OFF* */
const struct gps_type_t evermore_binary =
{
    .type_name      = "EverMore binary",	/* full name of type */
    .packet_type    = EVERMORE_PACKET,		/* lexer packet type */
    .trigger        = "$PEMT,", 		/* recognize the type */
    .channels       = EVERMORE_CHANNELS,	/* consumer-grade GPS */
    .probe_detect   = NULL,			/* no probe */
    .get_packet     = generic_get,		/* use generic one */
    .parse_packet   = evermore_parse_input,	/* parse message packets */
    .rtcm_writer    = pass_rtcm,		/* send RTCM data straight */
    .event_hook     = evermore_event_hook,	/* lifetime event handler */
#ifdef ALLOW_RECONFIGURE
    .speed_switcher = evermore_speed,		/* we can change baud rates */
    .mode_switcher  = evermore_mode,		/* there is a mode switcher */
    .rate_switcher  = evermore_rate_switcher,	/* change sample rate */
    .min_cycle      = 1,			/* ignore, no rate switch */
#endif /* ALLOW_RECONFIGURE */
#ifdef ALLOW_CONTROLSEND
    .control_send   = evermore_control_send,	/* how to send a control string */
#endif /* ALLOW_CONTROLSEND */
#ifdef NTPSHM_ENABLE
    .ntp_offset     = NULL,		/* no method for NTP fudge factor */
#endif /* NTPSHM_ ENABLE */
};
/* *INDENT-ON* */
#endif /* defined(EVERMORE_ENABLE) && defined(BINARY_ENABLE) */
