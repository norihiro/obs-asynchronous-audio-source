#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>
#include "plugin-macros.generated.h"

#define N_CHANNELS 2

#define STARTUP_TIMEOUT_MS 500

struct cfg_s
{
	int freq[N_CHANNELS];
	int skew_ppb;
	int rate;
};

struct source_s
{
	obs_source_t *context;

	// properties
	struct cfg_s cfg;

	pthread_t thread;
	pthread_mutex_t mutex;
	os_event_t *abort_event;
};

static const char *get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("Asynchronous Test Audio");
}

static obs_properties_t *get_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *props = obs_properties_create();
	obs_property_t *prop;

	obs_properties_add_int(props, "freq-0", obs_module_text("Frequency Left"), 10, 20000, 1);
	obs_properties_add_int(props, "freq-1", obs_module_text("Frequency Right"), 10, 20000, 1);
	prop = obs_properties_add_float(props, "skew", obs_module_text("Skew"), -5.0e4, +5.0e4, 1);
	obs_property_float_set_suffix(prop, obs_module_text("ppm"));
	obs_properties_add_int(props, "rate", obs_module_text("Sampling frequency"), 32000, 96000, 1000);

	return props;
}

static void get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "freq-0", 442);
	obs_data_set_default_int(settings, "freq-1", 442);
	obs_data_set_default_int(settings, "rate", 48000);
}

static void update(void *data, obs_data_t *settings)
{
	struct source_s *s = data;

	struct cfg_s cfg;

	for (int ch = 0; ch < N_CHANNELS; ch++) {
		char name[8];
		sprintf(name, "freq-%d", ch);
		cfg.freq[ch] = obs_data_get_int(settings, name);
	}

	cfg.skew_ppb = (int)(obs_data_get_double(settings, "skew") * 1e3);
	cfg.rate = obs_data_get_int(settings, "rate");

	pthread_mutex_lock(&s->mutex);
	s->cfg = cfg;
	pthread_mutex_unlock(&s->mutex);
}

static void *thread_main(void *data)
{
	struct source_s *s = data;

	const int frames = 128;
	struct obs_source_audio out = {
		.speakers = N_CHANNELS,
		.format = AUDIO_FORMAT_FLOAT_PLANAR,
		.frames = frames,
	};
	float *sample_data = bzalloc(frames * N_CHANNELS * sizeof(float));
	float *fltp[N_CHANNELS];
	for (int ch = 0; ch < N_CHANNELS; ch++) {
		fltp[ch] = sample_data + frames * ch;
		out.data[ch] = (void *)fltp[ch];
	}

	unsigned long timeout_ms = STARTUP_TIMEOUT_MS;

	struct cfg_s cfg = {
		.rate = 48000,
	};
	int t = 0;
	uint64_t first_ns = 0;
	uint64_t last_sent = 0;
	uint64_t n_output = 0;
	int last_sent_rem = 0;

	while (os_event_timedwait(s->abort_event, timeout_ms) == ETIMEDOUT) {

		for (int i = 0; i < frames; i++) {
			if (t == 0) {
				pthread_mutex_lock(&s->mutex);
				if (s->cfg.rate > 0)
					cfg = s->cfg;
				pthread_mutex_unlock(&s->mutex);
			}

			for (int ch = 0; ch < N_CHANNELS; ch++)
				fltp[ch][i] = sin(M_PI * 2 * t * cfg.freq[ch] / cfg.rate);

			if (++t >= cfg.rate)
				t = 0;
		}

		uint64_t cur = os_gettime_ns();
		uint64_t frame_ns = frames * 1000000000LL / cfg.rate;
		out.samples_per_sec = cfg.rate;
		out.timestamp = cur - frame_ns;
		obs_source_output_audio(s->context, &out);
		n_output += frames;

		if (t < frames) {
			blog(LOG_INFO, "output %.3f seconds audio, took %.3f seconds, skew=%f[ppm]",
			     (double)n_output / cfg.rate, (cur - first_ns) * 1e-9, cfg.skew_ppb * 1e-3);
		}

		if (!last_sent) {
			first_ns = cur;
			last_sent = cur;
			timeout_ms = frame_ns / 1000000;
		}
		else {
			uint64_t x = frames * (1000000000LL - cfg.skew_ppb) + last_sent_rem;
			last_sent += x / cfg.rate;
			last_sent_rem = x % cfg.rate;
			if (last_sent + frame_ns > cur)
				timeout_ms = (last_sent + frame_ns - cur) / 1000000;
			else
				timeout_ms = 0;
		}
	}

	bfree(sample_data);

	return NULL;
}

static void *create(obs_data_t *settings, obs_source_t *source)
{
	struct source_s *s = bzalloc(sizeof(struct source_s));
	s->context = source;
	pthread_mutex_init(&s->mutex, NULL);

	if (os_event_init(&s->abort_event, OS_EVENT_TYPE_MANUAL) != 0) {
		blog(LOG_ERROR, "Abort event creation failed!");
		goto cleanup;
	}

	update(s, settings);

	pthread_create(&s->thread, NULL, thread_main, s);

	return s;

cleanup:
	bfree(s);
	return NULL;
}

static void destroy(void *data)
{
	struct source_s *s = data;

	os_event_signal(s->abort_event);
	pthread_join(s->thread, NULL);

	os_event_destroy(s->abort_event);
	pthread_mutex_destroy(&s->mutex);

	bfree(s);
}

const struct obs_source_info asynchronous_audio_info = {
	.id = ID_PREFIX "asynchronous-audio-source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = get_name,
	.create = create,
	.destroy = destroy,
	.update = update,
	.get_properties = get_properties,
	.get_defaults = get_defaults,
	.icon_type = OBS_ICON_TYPE_AUDIO_INPUT,
};
