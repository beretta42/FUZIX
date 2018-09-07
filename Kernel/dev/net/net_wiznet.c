/*
 *	First draft of an implementation of the low level half of the
 *	network code for a WizNet device such as the 5200.
 *
 *	This isn't working code of any kind but a sketch of it to check
 *	the abstractions we currently have look workable.
 */

/* TODO 
  - server socket fixes (we should allocate one channel to the listening
    socket and when it connects allocate another one. That means we need to
    use to break the 1:1 mapping between wiznet and OS socket numbering
  - handle address collision interrupt
  - rework disconnect state machine
 */

#include <kernel.h>
#include <kdata.h>
#include <netdev.h>
#include <net_wiznet.h>
#include <printf.h>

#ifdef CONFIG_NET_WIZNET

static uint8_t irqmask;

#define RX_MASK 	0x07FF
#define TX_MASK		0x07FF

#define MR		0x0000
#define		MR_RESET	0x80
#define		MR_PB		0x10
#define		MR_PPPOE	0x08
#define		MR_AUTOINC	0x02
#define		MR_INDIRECT	0x01

#define GAR0		0x0001
#define SUBR0		0x0005
#define SHAR0		0x0009
#define SIPR0		0x000F
#define IR		0x0015
#define IMR		0x0016
#define RTR0		0x0017
#define RCR		0x0019
#define RMSR		0x001A
#define TMSR		0x001B
#define PATR0		0x001C
#define PTIMER		0x0028
#define PMAGIC		0x0029
#define UIPR0		0x002A
#define UPORT0		0x002E

#define Sn_MR		0x0400
#define Sn_CR		0x0401
#define 	OPEN		0x01
#define		LISTEN		0x02
#define		CONNECT		0x04
#define		DISCON		0x08
#define		CLOSE		0x10
#define		SEND		0x20
#define		SEND_MAC	0x21
#define		SEND_KEEP	0x22
#define		RECV		0x40
#define	Sn_IR		0x0402
#define		I_SEND_OK	0x10
#define		I_TIMEOUT	0x08
#define		I_RECV		0x04
#define		I_DISCON	0x02
#define		I_CON		0x01
#define Sn_SR		0x0403
#define		SOCK_CLOSED		0x00
#define		SOCK_ARP		0x01
#define		SOCK_INIT		0x13
#define		SOCK_LISTEN		0x14
#define		SOCK_SYNSENT		0x15
#define		SOCK_SYNRECV		0x16
#define		SOCK_ESTABLISHED	0x17
#define		SOCK_FIN_WAIT		0x18
#define		SOCK_CLOSING		0x1A
#define		SOCK_TIME_WAIT		0x1B
#define		SOCK_CLOSE_WAIT		0x1C
#define		SOCK_LAST_ACK		0x1D
#define		SOCK_UDP		0x22
#define		SOCK_IPRAW		0x32
#define		SOCK_MACROW		0x42
#define		SOC_PPPOE		0x5F
#define Sn_PORT0	0x0404
#define Sn_DHAR0	0x0406
#define Sn_DIPR0	0x040C
#define Sn_DPORT0	0x0410
#define Sn_MSSR0	0x0412
#define Sn_PROTO	0x0414
#define Sn_TOS		0x0415
#define Sn_TTL		0x0416
#define Sn_TX_FSR	0x0420
#define Sn_TX_RD0	0x0422
#define Sn_TX_WR0	0x0424
#define Sn_RX_RSR	0x0426
#define Sn_RX_RD0	0x0428




/* Core helpers: platform supplies wiz_bread{_u} and wiz_bwrite{_u} */

__sfr __at 0x28 mr;
__sfr __at 0x29 idm_ar0;
__sfr __at 0x2A idm_ar1;
__sfr __at 0x2B idm_dr;

/* We assume indirect, autoinc is always set */
static uint8_t wiz_readb(uint16_t off)
{
	idm_ar0 = off >> 8;
	idm_ar1 = off;
	return idm_dr;
}

static uint16_t wiz_readw(uint16_t off)
{
	uint16_t n;
	idm_ar0 = off >> 8;
	idm_ar1 = off;
	n = ((uint16_t)idm_dr) << 8;
	n |= idm_dr;
	return n;
}

static uint32_t wiz_readl(uint16_t off)
{
	uint32_t n;
	idm_ar0 = off >> 8;
	idm_ar1 = off;
	n = ((uint32_t)idm_dr) << 24;
	n |= ((uint32_t)idm_dr) << 16;
	n |= ((uint16_t)idm_dr) << 8;
	n |= idm_dr;
	return n;
}

static void wiz_bread(uint16_t off, uint8_t *p, uint8_t n)
{
	idm_ar0 = off >> 8;
	idm_ar1 = off;
	while(n--)
		*p++ = idm_dr;
}

static void wiz_breadu(uint16_t off, uint8_t *p, uint8_t n)
{
	idm_ar0 = off >> 8;
	idm_ar1 = off;
	while(n--)
		uputc(idm_dr, p++);
}

static void wiz_writeb(uint16_t off, uint8_t n)
{
	idm_ar0 = off >> 8;
	idm_ar1 = off;
	idm_dr = n;
}

static void wiz_writew(uint16_t off, uint16_t n)
{
	idm_ar0 = off >> 8;
	idm_ar1 = off;
	idm_dr = n >> 8;
	idm_dr = n;
}

static void wiz_writel(uint16_t off, uint32_t n)
{
	idm_ar0 = off >> 8;
	idm_ar1 = off;
	idm_dr = n >> 24;
	idm_dr = n >> 16;
	idm_dr = n >> 8;
	idm_dr = n;
}

static void wiz_bwrite(uint16_t off, uint8_t *p, uint8_t n)
{
	idm_ar0 = off >> 8;
	idm_ar1 = off;
	while(n--)
		idm_dr = *p++;
}

static void wiz_bwriteu(uint16_t off, uint8_t *p, uint8_t n)
{
	idm_ar0 = off >> 8;
	idm_ar1 = off;
	while(n--)
		idm_dr = ugetc(p++);
}

/* FIXME: look for ways to fold these four together */
static void wiz_queue(uint16_t i, uint16_t n, uint8_t * p)
{
	uint16_t dm = wiz_readw(Sn_TX_WR0 + i) & TX_MASK;
	uint16_t tx_base = 0x4000 + (i << 3);	/* i is already << 8 */

	if (dm + n >= TX_MASK) {
		uint16_t us = TX_MASK + 1 - dm;
		wiz_bwrite(dm + tx_base, p, us);
		wiz_bwrite(tx_base, p + us, n - us);
	} else
		wiz_bwrite(dm + tx_base, p, n);
}

static void wiz_queue_u(uint16_t i, uint16_t n, uint8_t * p)
{
	uint16_t dm = wiz_readw(Sn_TX_WR0 + i) & TX_MASK;
	uint16_t tx_base = 0x4000 + (i << 3);	/* i is already << 8 */

	if (dm + n >= TX_MASK) {
		uint16_t us = TX_MASK + 1 - dm;
		wiz_bwriteu(dm + tx_base, p, us);
		wiz_bwriteu(tx_base, p + us, n - us);
	} else
		wiz_bwriteu(dm + tx_base, p, n);
}

static void wiz_dequeue(uint16_t i, uint16_t n, uint8_t * p)
{
	uint16_t dm = wiz_readw(Sn_RX_RD0 + i) & RX_MASK;
	uint16_t rx_base = 0x6000 + (i << 3);	/* i is already << 8 */

	if (dm + n >= RX_MASK) {
		uint16_t us = RX_MASK + 1 - dm;
		wiz_bread(dm + rx_base, p, us);
		wiz_bread(rx_base, p + us, n - us);
	} else
		wiz_bread(dm + rx_base, p, n);
}

static void wiz_dequeue_u(uint16_t i, uint16_t n, uint8_t * p)
{
	uint16_t dm = wiz_readw(Sn_RX_RD0 + i) & RX_MASK;
	uint16_t rx_base = 0x6000 + (i << 3);	/* i is already << 8 */

	if (dm + n >= RX_MASK) {
		uint16_t us = RX_MASK + 1 - dm;
		wiz_breadu(dm + rx_base, p, us);
		wiz_breadu(rx_base, p + us, n - us);
	} else
		wiz_breadu(dm + rx_base, p, n);
}

static void wiz_wakeall(struct socket *s)
{
	wakeup(s);
	wakeup(&s->s_iflag);
	wakeup(&s->s_data);
}

static void wiz_eof(struct socket *s)
{
	s->s_iflag |= SI_EOF;
	wiz_wakeall(s);
}

/*
 *	Process interrupts from the WizNet device
 */
static void wiz_event_s(uint8_t i)
{
	struct socket *s = &sockets[i];
	uint16_t stat = wiz_readw(Sn_IR + (i << 8));	/* BE read of reg pair */

	if (stat & 0x1000) {
		/* Transmit completed: window re-open. We can allow more
		   data to flow from the user */
		s->s_iflag &= ~SI_THROTTLE;
		wiz_writeb(Sn_IR + (i << 8), 0x10);	/* Clear the flag down */
		wakeup(&s->s_data);
	}
	if (stat & 0x800) {
		/* Timeout */
		s->s_error = ETIMEDOUT;
		wiz_writeb(Sn_CR + (i << 8), CLOSE);
		wiz_wakeall(s);
		wiz_writeb(Sn_IR + (i << 8), 0x08);
		wiz_eof(s);
		/* Fall through and let CLOSE state processing do the work */
	}
	if (stat & 0x400) {
		/* Receive wake: Poke the user in case they are reading */
		s->s_iflag |= SI_DATA;
		wiz_writeb(Sn_IR + (i << 8), 0x04);	/* Clear the flag down */
		wakeup(&s->s_iflag);
	}
	if (stat & 0x200) {
		/* Disconnect: Just kill our host socket. Not clear if this
		   is right or we need to drain data first */
		wiz_writeb(Sn_IR + (i << 8), 0x02);	/* Clear the flag down */
		wiz_writeb(Sn_CR + (i << 8), CLOSE);
		wiz_eof(s);
		/* When we fall through we'll see CLOSE state and do the
		   actual shutting down */
	}
	if (stat & 0x100) {
		/* Connect: Move into connected state */
		if (s->s_state == SS_CONNECTING) {
			s->s_state = SS_CONNECTED;
			wakeup(s);
		}
		wiz_writeb(Sn_IR + (i << 8), 0x01);	/* Clear the flag down */
	}
	/* ??? return if high bits set here ?? */
	switch (stat & 0xFF) {
	case 0:		/* SOCK_CLOSED */
		if (s->s_state != SS_CLOSED && s->s_state != SS_UNUSED) {
			if (s->s_state != SS_CLOSING && s->s_state != SS_DEAD) {
				s->s_error = ECONNRESET;	/* Sort of a guess */
				wiz_wakeall(s);
			} else
				wakeup(s);
			irqmask &= ~(1 << i);
			wiz_writeb(IMR, irqmask);
			wiz_eof(s);
			/* Net layer wants us to burn the socket */
			if (s->s_state == SS_DEAD)
				sock_closed(s);
			else	/* so net_close() burns the socket */
				s->s_state = SS_CLOSED;
		}
		break;
	case 0x13:		/* SOCK_INIT */
		break;
	case 0x14:		/* SOCK_LISTEN */
		break;
	case 0x17:		/* SOCK_ESTABLISHED */
		if (s->s_state == SS_CONNECTING) {
			s->s_state == SS_CONNECTED;
			wakeup(s);
		} else if (s->s_state == SS_LISTENING) {
			/* TODO: We actually have to split the association between
			   wiznet and host sockets as we have multiple wiznet sockets
			   in the accept queue and a listener that is not really a
			   wiznet socket.. */
		}
		break;
	case 0x1C:		/* SOCK_CLOSE_WAIT */
		if (s->s_state == SS_CONNECTED
		    || s->s_state == SS_CONNECTING)
			s->s_state = SS_CLOSEWAIT;
		wiz_eof(s);
		if (s->s_state == SS_ACCEPTWAIT) {
			/* HUM ??? */
		}
		break;
	case 0x22:		/* SOCK_UDP */
	case 0x32:		/* SOCK_IPRAW */
	case 0x42:		/* SOCK_MACRAW */
		/* Socket has been created */
		s->s_state = SS_UNCONNECTED;
		wakeup(s);
		break;
	}
}

void wiz_event(void)
{
	uint8_t irq;
	uint8_t i = 0;
	struct socket *s = sockets;


	/* Polling cases */
	irq = wiz_readb(IR) & 0x0F;
	if (irq == 0)
		return;

	while (irq) {
		if (irq & 1)
			wiz_event_s(i);
		irq >>= 1;
		i++;
		s++;
	}
}

void wiz_poll(void)
{
	if (irqmask)
		wiz_event();
}

/* State management for creation of a socket. If need be allocate the socket
   on the IP offload device. May block */
int net_init(struct socket *s)
{
	s->s_state = SS_UNCONNECTED;
	return 0;
}

/* Bind a socket to an address. May block */
int net_bind(struct socket *s)
{
	uint16_t i = s - sockets;
	uint8_t r = SOCK_INIT;
	uint16_t off = i << 8;

	switch (s->s_type) {
	case SOCKTYPE_TCP:
		wiz_writeb(Sn_MR + off, 0x21);	/* TCP, delayed ack */
		/* We keep ports net endian so don't byte swap */
		wiz_bwrite(Sn_PORT0 + off, &s->s_addr[SADDR_SRC].port, 2);
		break;
	case SOCKTYPE_UDP:
		wiz_writeb(Sn_MR + off, 0x02);	/* UDP */
		wiz_bwrite(Sn_PORT0 + off, &s->s_addr[SADDR_SRC].port, 2);
		r = SOCK_UDP;
		break;
	case SOCKTYPE_RAW:
		wiz_writeb(Sn_PROTO + off, s->s_addr[SADDR_SRC].port);	/* hack */
		wiz_writeb(Sn_MR + off, 0x03);	/* RAW */
		r = SOCK_IPRAW;
	}
	/* Make an open request to open the socket */
	wiz_writeb(Sn_CR + off, OPEN);

	/* If the reply is not immediately SOCK_INT we failed */
	if (wiz_readb(Sn_SR + off) != r) {
		udata.u_error = EADDRINUSE;	/* Something broke ? */
		return -1;
	}
	/* Interrupt on if available mark as bound */
	irqmask |= 1 << i;
	wiz_writeb(IMR, irqmask);
	s->s_state = SS_BOUND;
	/* Do we need to delay the SS_BOUND until the chip interrupts ? */
	return 0;
}

/* Start a socket listening. This expects BSD unix like semantics so you
   may well need to map between host and offload sockets. May block. */

/* This isn't quite right - we need to allocate a free socket and use it
   as the listener and keep doing so when one comes in. The listening socket
   in fact has no real binding to the wiz one - fixme and use s->s_lcn for
   channel mapping */
int net_listen(struct socket *s)
{
	uint16_t i = s - sockets;

	i <<= 8;

	/* Issue a listen command. Check the state went to SOCK_LISTEN */
	wiz_writeb(Sn_CR + i, LISTEN);
	if (wiz_readb(Sn_SR + i) != SOCK_LISTEN) {
		udata.u_error = EIO;//FIXME EPROTO;	/* ??? */
		return -1;
	}
	s->s_state = SS_LISTENING;
	return 0;
}

/* Start connecting to a remote host. Should not block. Return SS_CONNECTING
   state if in progress, SS_CONNECTED if done immediately, other states for
   error fails. */
int net_connect(struct socket *s)
{

	if (s->s_type == SOCKTYPE_TCP) {
		uint16_t i;
		i = s - sockets;
		i <<= 8;
		/* Already net endian */
		wiz_bwrite(Sn_DIPR0 + i, &s->s_addr[SADDR_DST].addr, 4);
		wiz_bwrite(Sn_DPORT0 + i, &s->s_addr[SADDR_DST].port, 2);
		wiz_writeb(Sn_CR + i, CONNECT);
		s->s_state = SS_CONNECTING;
	} else {
		/* UDP/RAW - note have to do our own filtering for 'connect' */
		s->s_state = SS_CONNECTED;
	}
	return 0;
}

/* Close down a socket - preferably politely */
void net_close(struct socket *s)
{
	uint16_t i = s - sockets;
	uint16_t off = i << 8;

	if (s->s_type == SOCKTYPE_TCP && s->s_state != SS_CLOSED) {
		wiz_writeb(Sn_CR + off, DISCON);
		s->s_state = SS_CLOSING;
	} else {
		irqmask &= ~(1 << i);
		wiz_writeb(IMR, irqmask);
		wiz_writeb(Sn_CR + off, CLOSE);
		sock_closed(s);
	}
}

arg_t net_read(struct socket *s, uint8_t flag)
{
	uint16_t n = 0xFFFF;
	uint16_t r;
	uint16_t i = s - sockets;
	uint8_t st;

	i <<= 8;

	s->s_iflag &= ~SI_DATA;

	/* FIXME: IRQ protection */
	/* Wait for data - push int core code ? */
	while ((s->s_iflag & SI_DATA) == 0) {
		/* See if we have lost the link */
		if (s->s_state < SS_CONNECTED) {
			udata.u_error = EINVAL;
			return -1;
		}
		/* Check for an EOF (covers post close cases too) */
		if (s->s_iflag & SI_EOF)
			return 0;
		/* Keep waiting until we get the right state */
		/* Bytes available */
		n = wiz_readw(Sn_RX_RSR + i);
		if (n) {
			s->s_iflag |= SI_DATA;
			break;
		}
		st = wiz_readb(Sn_SR);
		if (st >= SOCK_CLOSING && st <= SOCK_UDP)
			return 0;
		/* Need IRQ protection to avoid sleep race */
		if (psleep_flags(&s->s_iflag, flag))
			return -1;
	}
	switch (s->s_type) {
	case SOCKTYPE_RAW:
	case SOCKTYPE_UDP:
		/* UDP comes with a header */
		wiz_dequeue(i, 4, (uint8_t *) & s->s_addr[SADDR_TMP].addr);
		if (s->s_type == SOCKTYPE_UDP)
			wiz_dequeue(i, 2,
				    (uint8_t *) & s->s_addr[SADDR_TMP].
				    port);
		wiz_dequeue(i, 2, (uint8_t *) & n);	/* Actual packet size */
		n = ntohs(n);	/* Big endian on device */
		/* Fall through */
	case SOCKTYPE_TCP:
		/* Bytes to consume */
		r = min(n, udata.u_count);
		/* Now dequeue some bytes into udata.u_base */
		wiz_dequeue_u(i, r, udata.u_base);
		/* For datagrams we always discard the entire frame */
		if (s->s_type == SOCKTYPE_UDP)
			r = n + 8;
		wiz_writew(Sn_RX_RD0 + i, wiz_readw(Sn_RX_RD0 + i) + r);
		/* FIXME: figure out if SI_DATA should be cleared */
		/* Now tell the device we ate the data */
		wiz_writeb(Sn_CR, RECV);
	}
	return r;
}

arg_t net_write(struct socket * s, uint8_t flag)
{
	uint16_t i = s - sockets;
	uint16_t room;
	uint16_t n = 0;;
	uint8_t a = s->s_flag & SFLAG_ATMP ? SADDR_TMP : SADDR_DST;

	/* FIXME: blocking ?? */
	used(flag);

	i <<= 8;

	room = wiz_readw(Sn_TX_FSR + i);

	switch (s->s_type) {
	case SOCKTYPE_UDP:
		if (udata.u_count > 1472) {
			udata.u_error = EMSGSIZE;
			return -1;
		}
	case SOCKTYPE_RAW:
		if (udata.u_count > 1500) {
			udata.u_error = EMSGSIZE;
			return -1;
		}
		if (room < udata.u_count)
			return -2;
		wiz_writel(Sn_DIPR0 + i, s->s_addr[a].addr);
		wiz_writel(Sn_DPORT0 + i, s->s_addr[a].port);
		/* Fall through */
	case SOCKTYPE_TCP:
		if (room == 0)
			return -2;
		n = min(room, udata.u_count);
		wiz_queue_u(i, n, udata.u_base);
		wiz_writew(Sn_TX_WR0 + i,
			     wiz_readw(Sn_TX_WR0 + i) + n);
		wiz_writeb(Sn_CR, SEND);
		break;
	}
	return n;
}

arg_t net_shutdown(struct socket *s, uint8_t flag)
{
	s->s_iflag |= flag;
	if (s->s_iflag & SI_SHUTW)
		wiz_writeb(Sn_CR, DISCON);
	/* Really we need to look for SHUTR and received data and CLOSE if
	   so - FIXME */
	return 0;
}

/* Everything below this line is still pure sketching of ideas as we don't
   really have a configuration interface designed yet ! */

struct netdevice net_dev = {
	6,			/* MAC size */
	"eth0",			/* Good a name as any */
	0,			/* No special flags */
};
#if 0
/* Only some of these hit this code, most are handled by the core */
arg_t net_ioctl(uint8_t op, void *p)
{
	uint16_t n;

	switch (op) {
	case OP_SIFADDR:
		wiz_bwrite(SIPR0, p, 4);
		break;
	case OP_SIFMASK:
		wiz_bwrite(SUBR0, p, 4);
		break;
	case OP_SIFGW:
		wiz_bwrite(GAR0, p, 4);
		break;
	case OP_GIFHWADDR:
		wiz_bread(SHAR0, p, 6);
		break;
	case OP_SIFHWADDR:
		wiz_bwrite(SHAR0, p, 6);
		break;
	case OP_GPHY:
		return (wiz_readb(PSTATUS) & 0x20) ? LINK_UP : LINK_DOWN;
	default:
		return -EINVAL;
	}
	return 0;
}
#endif
static uint32_t ipa = 0x00000000;	/* Tmp hack */
static uint8_t fakeaddr[6] = { 0xC0, 0xFF, 0xEE, 0xC0, 0xFF, 0xEE };
static uint32_t iga = 0x020000C0;
static uint32_t igm = 0x00FFFFFF;

void netdev_init(void)
{
	uint16_t i;
	/* We run all the time in indirect, autoinc */
	mr = MR_AUTOINC|MR_INDIRECT;
	wiz_writeb(IMR, 0);
//	wiz_writeb(RTR, );
//	wiz_writeb(RCR, );
	/* Set GAR, SHAR, SUBR, SIPR to defaults ? */
	wiz_bwrite(SIPR0, &ipa, 4);
	wiz_bwrite(GAR0, &iga, 4);
	wiz_bwrite(SUBR0, &igm, 4);
	wiz_bwrite(SHAR0, fakeaddr, 6);
	wiz_writeb(RMSR, 0x55);	/* 2k a socket */
	wiz_writeb(TMSR, 0x55);	/* 2k a socket */
	for (i = 0; i < 4 * 256; i += 256) {
		/* Do we need to set anything here */
	}
}

#endif
