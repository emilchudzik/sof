/* 
 * BSD 3 Clause - See LICENCE file for details.
 *
 * Copyright (c) 2015, Intel Corporation
 * All rights reserved.
 *
 * Authors:	Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *
 * Intel PMC IPC.
 */

#include <reef/debug.h>
#include <reef/timer.h>
#include <reef/interrupt.h>
#include <reef/ipc.h>
#include <reef/reef.h>
#include <reef/alloc.h>
#include <reef/wait.h>
#include <reef/trace.h>
#include <platform/interrupt.h>
#include <platform/shim.h>



/* private data for IPC */
struct intel_ipc_pmc_data {
	uint32_t msg_l;
	uint32_t msg_h;
	uint32_t pending;
};


static struct intel_ipc_pmc_data *_pmc;

static void do_cmd(void)
{
	uint32_t ipcsc, status = 0;
	
	trace_ipc("SCm");
	trace_value(_pmc->msg_l);

	//status = ipc_cmd();
	_pmc->pending = 0;

	/* clear BUSY bit and set DONE bit - accept new messages */
	ipcsc = shim_read(SHIM_IPCSCH);
	ipcsc &= ~SHIM_IPCSCH_BUSY;
	ipcsc |= SHIM_IPCSCH_DONE | status;
	shim_write(SHIM_IPCSCH, ipcsc);

	/* unmask busy interrupt */
	shim_write(SHIM_IMRLPESC, shim_read(SHIM_IMRLPESC) & ~SHIM_IMRLPESC_BUSY);
}


/* process current message */
int pmc_process_msg_queue(void)
{
	if (_pmc->pending)
		do_cmd();
	return 0;
}

static void do_notify(void)
{
	trace_ipc("SNo");

	/* clear DONE bit  */
	shim_write(SHIM_IPCLPESCH, shim_read(SHIM_IPCLPESCH) & ~SHIM_IPCLPESCH_DONE);

	/* unmask Done interrupt */
	shim_write(SHIM_IMRLPESC, shim_read(SHIM_IMRLPESC) & ~SHIM_IMRLPESC_DONE);
}

static void irq_handler(void *arg)
{
	uint32_t isrlpesc;

	trace_ipc("SIQ");

	/* Interrupt arrived, check src */
	isrlpesc = shim_read(SHIM_ISRLPESC);

	if (isrlpesc & SHIM_ISRLPESC_DONE) {

		/* Mask Done interrupt before return */
		shim_write(SHIM_IMRLPESC, shim_read(SHIM_IMRLPESC) | SHIM_IMRLPESC_DONE);
		interrupt_clear(IRQ_NUM_EXT_PMC);
		do_notify();
	}

	if (isrlpesc & SHIM_ISRLPESC_BUSY) {
		
		/* Mask Busy interrupt before return */
		shim_write(SHIM_IMRLPESC, shim_read(SHIM_IMRLPESC) | SHIM_IMRLPESC_BUSY);
		interrupt_clear(IRQ_NUM_EXT_PMC);

		/* place message in Q and process later */
		_pmc->msg_l = shim_read(SHIM_IPCSCL);
		_pmc->msg_h = shim_read(SHIM_IPCSCH);
		_pmc->pending = 1;
	}
}

int ipc_pmc_send_msg(uint32_t message)
{
	uint32_t ipclpesch, irq_mask;

	trace_ipc("SMs");

	ipclpesch = shim_read(SHIM_IPCLPESCH);

	/* we can only send new messages if the SC is not busy */
	if (ipclpesch & SHIM_IPCLPESCH_BUSY)
		return -EAGAIN;

	/* disable all interrupts except for SCU */
	irq_mask = arch_interrupt_get_enabled();
	arch_interrupt_enable_mask(1 << IRQ_NUM_EXT_PMC);

	/* send the new message */
	shim_write(SHIM_IPCLPESCL, 0);
	shim_write(SHIM_IPCLPESCH, SHIM_IPCLPESCH_BUSY | message);

	/* now wait for clock change */
	wait_for_interrupt(0);

	arch_interrupt_enable_mask(irq_mask);
	return 0;
}

int platform_ipc_pmc_init(void)
{
	uint32_t imrlpesc;

	/* init ipc data */
	_pmc = rmalloc(RZONE_DEV, RMOD_SYS, sizeof(struct intel_ipc_pmc_data));

	/* configure interrupt */
	interrupt_register(IRQ_NUM_EXT_PMC, irq_handler, NULL);
	interrupt_enable(IRQ_NUM_EXT_PMC);

	/* Unmask Busy and Done interrupts */
	imrlpesc = shim_read(SHIM_IMRLPESC);
	imrlpesc &= ~(SHIM_IMRLPESC_BUSY | SHIM_IMRLPESC_DONE);
	shim_write(SHIM_IMRLPESC, imrlpesc);

	return 0;
}
