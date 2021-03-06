/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MDP5_KMS_H__
#define __MDP5_KMS_H__

#include "msm_drv.h"
#include "msm_kms.h"
#include "mdp/mdp_kms.h"
#include "mdp5_cfg.h"	/* must be included before mdp5.xml.h */
#include "mdp5.xml.h"
#include "mdp5_ctl.h"
#include "mdp5_smp.h"

struct mdp5_kms {
	struct mdp_kms base;

	struct drm_device *dev;

	struct mdp5_cfg_handler *cfg;

	/* mapper-id used to request GEM buffer mapped for scanout: */
	int id;
	struct msm_mmu *mmu;

	struct mdp5_smp *smp;
	struct mdp5_ctl_manager *ctlm;

	/* io/register spaces: */
	void __iomem *mmio, *vbif;

	struct regulator *vdd;

	struct clk *axi_clk;
	struct clk *ahb_clk;
	struct clk *src_clk;
	struct clk *core_clk;
	struct clk *lut_clk;
	struct clk *vsync_clk;

	/*
	 * lock to protect access to global resources: ie., following register:
	 *	- REG_MDP5_DISP_INTF_SEL
	 */
	spinlock_t resource_lock;

	struct mdp_irq error_handler;

	struct {
		volatile unsigned long enabled_mask;
		struct irq_domain *domain;
	} irqcontroller;
};
#define to_mdp5_kms(x) container_of(x, struct mdp5_kms, base)

struct mdp5_plane_state {
	struct drm_plane_state base;

	/* "virtual" zpos.. we calculate actual mixer-stage at runtime
	 * by sorting the attached planes by zpos and then assigning
	 * mixer stage lowest to highest.  Private planes get default
	 * zpos of zero, and public planes a unique value that is
	 * greater than zero.  This way, things work out if a naive
	 * userspace assigns planes to a crtc without setting zpos.
	 */
	int zpos;

	/* the actual mixer stage, calculated in crtc->atomic_check()
	 * NOTE: this should move to mdp5_crtc_state, when that exists
	 */
	enum mdp_mixer_stage_id stage;

	/* some additional transactional status to help us know in the
	 * apply path whether we need to update SMP allocation, and
	 * whether current update is still pending:
	 */
	bool mode_changed : 1;
	bool pending : 1;
};
#define to_mdp5_plane_state(x) \
		container_of(x, struct mdp5_plane_state, base)

static inline void mdp5_write(struct mdp5_kms *mdp5_kms, u32 reg, u32 data)
{
	msm_writel(data, mdp5_kms->mmio + reg);
}

static inline u32 mdp5_read(struct mdp5_kms *mdp5_kms, u32 reg)
{
	return msm_readl(mdp5_kms->mmio + reg);
}

static inline const char *pipe2name(enum mdp5_pipe pipe)
{
	static const char *names[] = {
#define NAME(n) [SSPP_ ## n] = #n
		NAME(VIG0), NAME(VIG1), NAME(VIG2),
		NAME(RGB0), NAME(RGB1), NAME(RGB2),
		NAME(DMA0), NAME(DMA1),
		NAME(VIG3), NAME(RGB3),
#undef NAME
	};
	return names[pipe];
}

static inline int pipe2nclients(enum mdp5_pipe pipe)
{
	switch (pipe) {
	case SSPP_RGB0:
	case SSPP_RGB1:
	case SSPP_RGB2:
	case SSPP_RGB3:
		return 1;
	default:
		return 3;
	}
}

static inline uint32_t intf2err(int intf)
{
	switch (intf) {
	case 0:  return MDP5_IRQ_INTF0_UNDER_RUN;
	case 1:  return MDP5_IRQ_INTF1_UNDER_RUN;
	case 2:  return MDP5_IRQ_INTF2_UNDER_RUN;
	case 3:  return MDP5_IRQ_INTF3_UNDER_RUN;
	default: return 0;
	}
}

static inline uint32_t intf2vblank(int intf)
{
	switch (intf) {
	case 0:  return MDP5_IRQ_INTF0_VSYNC;
	case 1:  return MDP5_IRQ_INTF1_VSYNC;
	case 2:  return MDP5_IRQ_INTF2_VSYNC;
	case 3:  return MDP5_IRQ_INTF3_VSYNC;
	default: return 0;
	}
}

int mdp5_disable(struct mdp5_kms *mdp5_kms);
int mdp5_enable(struct mdp5_kms *mdp5_kms);

void mdp5_set_irqmask(struct mdp_kms *mdp_kms, uint32_t irqmask);
void mdp5_irq_preinstall(struct msm_kms *kms);
int mdp5_irq_postinstall(struct msm_kms *kms);
void mdp5_irq_uninstall(struct msm_kms *kms);
irqreturn_t mdp5_irq(struct msm_kms *kms);
int mdp5_enable_vblank(struct msm_kms *kms, struct drm_crtc *crtc);
void mdp5_disable_vblank(struct msm_kms *kms, struct drm_crtc *crtc);
int mdp5_irq_domain_init(struct mdp5_kms *mdp5_kms);
void mdp5_irq_domain_fini(struct mdp5_kms *mdp5_kms);

static inline
uint32_t mdp5_get_formats(enum mdp5_pipe pipe, uint32_t *pixel_formats,
		uint32_t max_formats)
{
	/* TODO when we have YUV, we need to filter supported formats
	 * based on pipe id..
	 */
	return mdp_get_formats(pixel_formats, max_formats);
}

uint32_t mdp5_plane_get_flush(struct drm_plane *plane);
void mdp5_plane_complete_flip(struct drm_plane *plane);
enum mdp5_pipe mdp5_plane_pipe(struct drm_plane *plane);
struct drm_plane *mdp5_plane_init(struct drm_device *dev,
		enum mdp5_pipe pipe, bool private_plane, uint32_t reg_offset);

uint32_t mdp5_crtc_vblank(struct drm_crtc *crtc);

int mdp5_crtc_get_lm(struct drm_crtc *crtc);
void mdp5_crtc_cancel_pending_flip(struct drm_crtc *crtc, struct drm_file *file);
void mdp5_crtc_set_intf(struct drm_crtc *crtc, int intf,
		enum mdp5_intf intf_id);
struct drm_crtc *mdp5_crtc_init(struct drm_device *dev,
		struct drm_plane *plane, int id);

struct drm_encoder *mdp5_encoder_init(struct drm_device *dev, int intf,
		enum mdp5_intf intf_id);

#endif /* __MDP5_KMS_H__ */
