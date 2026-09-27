(function (global) {
  const colorFields = new Set([
    'spotlightsColor',
    'hourColor',
    'minColor',
    'colonColor',
    'dayColor',
    'separatorColor',
    'monthColor',
    'tempColor',
    'degreeColor',
    'typeColor',
    'humiColor',
    'humiDecimalColor',
    'humiSymbolColor',
    'countdownColor',
    'scoreboardColorLeft',
    'scoreboardColorRight',
    'spectrumBackgroundColor',
    'spectrumColor',
    'scrollColor'
  ]);

  const aliasMap = {
    clock: 'hourColor',
    minutes: 'minColor',
    colon: 'colonColor',
    day: 'dayColor',
    month: 'monthColor',
    separator: 'separatorColor',
    temp: 'tempColor',
    degree: 'degreeColor',
    type: 'typeColor',
    humidity: 'humiColor',
    humidityDecimal: 'humiDecimalColor',
    humiditySymbol: 'humiSymbolColor',
    countdown: 'countdownColor',
    scoreboardLeft: 'scoreboardColorLeft',
    scoreboardRight: 'scoreboardColorRight',
    spectrum: 'spectrumColor',
    spectrumBackground: 'spectrumBackgroundColor',
    spotlights: 'spotlightsColor',
    scroll: 'scrollColor'
  };

  const numericInputTypes = new Set(['number', 'range']);

  let cache = null;
  let loadingPromise = null;

  function resolveKey(key) {
    return aliasMap[key] || key;
  }

  function normalizeColor(value) {
    if (typeof value === 'string' && /^#([0-9a-f]{6})$/i.test(value)) {
      return value;
    }

    const numeric = Number(value);
    if (!Number.isFinite(numeric)) {
      return '#000000';
    }

    const clamped = numeric & 0xffffff;
    return `#${clamped.toString(16).padStart(6, '0')}`;
  }

  async function ensureCache() {
    if (cache) {
      return cache;
    }

    if (!loadingPromise) {
      loadingPromise = global.Api.getSettings()
        .then((data) => {
          cache = data || {};
          return cache;
        })
        .finally(() => {
          loadingPromise = null;
        });
    }

    return loadingPromise;
  }

  function getCachedValue(key) {
    if (!cache) {
      return undefined;
    }
    return cache[key];
  }

  function normalizeValueForInput(key, value) {
    if (colorFields.has(key)) {
      return normalizeColor(value);
    }
    return value;
  }

  function prepareForSend(key, value, meta = {}) {
    const element = meta.element;
    if (colorFields.has(key)) {
      if (typeof value === 'string' && value.startsWith('#')) {
        return parseInt(value.slice(1), 16);
      }
      return Number(value) || 0;
    }

    if (element) {
      const type = element.type;
      if (type === 'checkbox') {
        return Boolean(value);
      }
      if (numericInputTypes.has(type)) {
        const num = Number(value);
        return Number.isFinite(num) ? num : value;
      }
    }

    return value;
  }

  async function updateValue(key, value, meta = {}) {
    await ensureCache();
    const resolvedKey = resolveKey(key);
    const payloadValue = prepareForSend(resolvedKey, value, meta);
    const body = { [resolvedKey]: payloadValue };
    await global.Api.postUpdate(body);
    cache[resolvedKey] = payloadValue;
    return payloadValue;
  }

  async function updateFromInput(key, element) {
    if (!element) {
      return;
    }

    if (element.type === 'radio' && !element.checked) {
      return;
    }

    const value = element.type === 'checkbox' ? element.checked : element.value;
    return updateValue(key, value, { element });
  }

  async function updateNested(key, updates) {
    await ensureCache();
    const resolvedKey = resolveKey(key);
    const current = { ...(cache[resolvedKey] || {}) };
    Object.assign(current, updates);
    const body = { [resolvedKey]: current };
    await global.Api.postUpdate(body);
    cache[resolvedKey] = current;
    return current;
  }

  function getValue(key) {
    const resolvedKey = resolveKey(key);
    return getCachedValue(resolvedKey);
  }

  function getNormalizedValue(key) {
    const resolvedKey = resolveKey(key);
    const value = getCachedValue(resolvedKey);
    return normalizeValueForInput(resolvedKey, value);
  }

  function getColor(key) {
    const resolvedKey = resolveKey(key);
    const value = getCachedValue(resolvedKey);
    if (value === undefined) {
      return undefined;
    }
    return normalizeColor(value);
  }

  function clearCache() {
    cache = null;
    loadingPromise = null;
  }

  const SettingsStore = {
    async load(force = false) {
      if (force) {
        clearCache();
      }
      return ensureCache();
    },
    getValue,
    getNormalizedValue,
    getColor,
    updateValue,
    updateFromInput,
    updateNested,
    clearCache
  };

  global.SettingsStore = SettingsStore;
})(window);
