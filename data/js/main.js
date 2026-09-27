const { Api, SettingsStore } = window;
const radioButtons = {
    'pastelColors': 'pastelColors',
    'suspendType': 'suspendType',
    'spotlightsColorSettings': 'spotlightsColorSettings',
    'ClockDisplayType': 'ClockDisplayType',
    'colonType': 'colonType',
    'ClockColorSettings': 'ClockColorSettings',
    'dateDisplayType': 'dateDisplayType',
    'DateColorSettings': 'DateColorSettings', 
    'temperatureSymbol': 'temperatureSymbol',
    'tempDisplayType': 'tempDisplayType',
    'tempColorSettings': 'tempColorSettings',
    'humiDisplayType': 'humiDisplayType',
    'humiColorSettings': 'humiColorSettings',
    'spectrumBackgroundSettings': 'spectrumBackgroundSettings',
    'spectrumColorSettings': 'spectrumColorSettings',
    'scrollColorSettings': 'scrollColorSettings'
};
const dropDownSelectors = {
    'ColorChangeFrequency': 'ColorChangeFrequency',
    'suspendFrequency': 'suspendFrequency',
    'gmtOffset_sec': 'gmtOffset_sec',
    'temperatureCorrection': 'temperatureCorrection',
    'scrollFrequency': 'scrollFrequency',
    'defaultAudibleAlarm': 'song'
}
const directValueSelectors = {
    'rangeBrightness': 'rangeBrightness',
    'spotlightsColor': 'spotlightsColor',
    'hourColor': 'hourColor',
    'minColor': 'minColor',
    'colonColor': 'colonColor',
    'dayColor': 'dayColor',
    'separatorColor': 'separatorColor',
    'monthColor': 'monthColor',
    'tempColor': 'tempColor',
    'degreeColor': 'degreeColor',
    'typeColor': 'typeColor',
    'humiColor': 'humiColor',
    'humiDecimalColor': 'humiDecimalColor',
    'humiSymbolColor': 'humiSymbolColor',
    'countdownColor': 'countdownColor',
    'scoreboardColorLeft': 'scoreboardColorLeft',
    'scoreboardColorRight': 'scoreboardColorRight',
    'spectrumBackgroundColor': 'spectrumBackgroundColor',
    'spectrumColor': 'spectrumColor',
    'scrollText': 'scrollText',
    'scrollColor': 'scrollColor',
    'stationID': 'stationID'
}
const checkboxSelectors = {
    'useSpotlights': 'useSpotlights',
    'DSTime': 'DSTime',
    'useAudibleAlarm': 'useAudibleAlarm',
    'humidity_outdoor_enable': 'humidity_outdoor_enable',
    'temperature_outdoor_enable': 'temperature_outdoor_enable',
    'colorchangeCD': 'colorchangeCD',
    'randomSpectrumMode': 'randomSpectrumMode',
    'scrollOptions1': 'scrollOptions1',
    'scrollOptions2': 'scrollOptions2',
    'scrollOptions3': 'scrollOptions3',
    'scrollOptions4': 'scrollOptions4',
    'scrollOptions5': 'scrollOptions5',
    'scrollOptions6': 'scrollOptions6',
    'scrollOptions7': 'scrollOptions7',
    'scrollOptions8': 'scrollOptions8',
    'scrollOptions9': 'scrollOptions9',
    'scrollOverride': 'scrollOverride'
}

function hideElementById(id) {
    const element = document.getElementById(id);
    if (element) {
        element.style.display = 'none';
    }
}

function setElementsHidden(selector, hidden) {
    document.querySelectorAll(selector).forEach(element => {
        if (hidden) {
            element.setAttribute('hidden', true);
        } else {
            element.removeAttribute('hidden');
        }
    });
}

function weatherApiConfigured(settings) {
    const api = settings && settings.weatherapi;
    if (!api) {
        return false;
    }

    const hasLatitude = String(api.latitude || '').trim().length > 0;
    const hasLongitude = String(api.longitude || '').trim().length > 0;
    const hasApiKey = String(api.apikey || '').trim().length > 0;
    return hasLatitude && hasLongitude && (!settings.HAS_ONLINEWEATHER || hasApiKey);
}

document.addEventListener('DOMContentLoaded', function () {
     
    /* GLOBAL */
    document.querySelector("nav button").addEventListener('click', function() {
        let navDiv = document.querySelector("nav");
        if (navDiv.classList.contains("show")) {
            navDiv.classList.remove("show");
        } else {
            navDiv.classList.add("show");
        }
    });

    document.querySelectorAll("input[type='button'][data-param]").forEach((element) => {
        element.addEventListener('click', async event => {
            let body = {};
            body[event.target.dataset.param] = true;
            try {
                await Api.postUpdate(body);
            } catch (error) {
                console.error('Failed to update parameter button', error);
            }
        });
    })

    /* HOME STUFF */
    if (document.querySelectorAll("form[name='shelfclock-home']").length > 0) {

        /* load home */
        loadHome();
        
        document.querySelectorAll("[data-showspectrum]").forEach(element => {
            element.addEventListener('click', async event => {
                let body = {
                    spectrumMode: event.target.dataset.showspectrum
                }
                try {
                    await Api.postUpdate(body);
                } catch (error) {
                    console.error('Failed to update spectrum mode', error);
                }
            });
        });

        document.querySelectorAll("[data-lightshow]").forEach(element => {
            element.addEventListener('click', async event => {
                let body = {
                    lightshowMode: event.target.dataset.lightshow
                }
                try {
                    await Api.postUpdate(body);
                } catch (error) {
                    console.error('Failed to update lightshow mode', error);
                }
            });
        });
        
        document.querySelectorAll("input[name='countdown'], input[name='stopwatch']").forEach(element => {
            element.addEventListener('click', async event => {
                let post = (event.target.name === 'stopwatch') ? "StopwatchMode" : "CountdownMode";
                let body = {};
                let hours = document.querySelector("[name='hours']").value;
                let minutes = document.querySelector("[name='minutes']").value;
                let seconds = document.querySelector("[name='seconds']").value;
                var ms = (hours * 60 * 60 * 1000) + (minutes * 60 * 1000) + (seconds * 1000);
                body[post] = ms;
                try {
                    await Api.postUpdate(body);
                } catch (error) {
                    console.error('Failed to update timer mode', error);
                }
            });
        });

        document.querySelectorAll("input[name='left'], input[name='right']").forEach(element => {
            element.addEventListener('change', async event => {
                let body = {
                    'ScoreboardMode': {
                        'left': document.querySelector("input[name='left']").value,
                        'right': document.querySelector("input[name='right']").value
                    }
                };
                try {
                    await Api.postUpdate(body);
                } catch (error) {
                    console.error('Failed to update scoreboard values', error);
                }
            })
        });

        document.querySelectorAll("input[name='upleft'], input[name='upright'], input[name='downleft'], input[name='downright']").forEach(element => {
            element.addEventListener('click', async event => {
                let left = document.querySelector("input[name='left']");
                let right = document.querySelector("input[name='right']");
                switch (event.target.name) {
                    case 'upleft': left.value = (left.value === "99") ? "99" : parseInt(left.value) + 1; break;
                    case 'upright': right.value = (right.value === "99") ? "99" : parseInt(right.value) + 1; break;
                    case 'downleft': left.value = (left.value === "0") ? "0" : parseInt(left.value) - 1; break;
                    case 'downright': right.value = (right.value === "0") ? "0" : parseInt(right.value) - 1; break;
                }
                if (event.target.name === 'upleft' || event.target.name === 'downleft') {
                    left.dispatchEvent(new Event('change'));
                } else {
                    right.dispatchEvent(new Event('change'));
                }
            })
        });
    }

    /* SETTINGS STUFF */
    if (document.querySelectorAll("form[name='shelfclock-settings']").length > 0) {

        (async () => {
            try {
                await loadSettings();
            } catch (error) {
                console.error('Failed to initialise settings', error);
                return;
            }

            const attachUpdate = (key, element, eventName = 'change') => {
                if (!element) {
                    return;
                }
                element.addEventListener(eventName, async event => {
                    try {
                        await SettingsStore.updateFromInput(key, event.target);
                    } catch (err) {
                        console.error(`Failed to update setting ${key}`, err);
                    }
                });
            };

            for (let [key, value] of Object.entries(radioButtons)) {
                document.querySelectorAll(`input[name='${value}']`).forEach(element => {
                    attachUpdate(key, element, 'change');
                });
            }

            for (let [key, value] of Object.entries(dropDownSelectors)) {
                document.querySelectorAll(`select[name='${value}']`).forEach(element => {
                    attachUpdate(key, element, 'change');
                });
            }

            for (let [key, value] of Object.entries(directValueSelectors)) {
                const element = document.querySelector(`[name='${value}']`);
                if (!element) continue;
                const handler = debounce(async event => {
                    try {
                        await SettingsStore.updateFromInput(key, event.target);
                    } catch (err) {
                        console.error(`Failed to update setting ${key}`, err);
                    }
                }, 500);
                const eventName = element.type === 'color' ? 'change' : 'input';
                element.addEventListener(eventName, handler);
            }

            for (let [key, value] of Object.entries(checkboxSelectors)) {
                const element = document.querySelector(`[name='${value}']`);
                attachUpdate(key, element, 'change');
            }

            const datetimeButton = document.querySelector("[name='setdatetime']");
            if (datetimeButton) {
                datetimeButton.addEventListener('click', async () => {
                    let date = new Date();
                    let body = {
                        setdate: {
                            year: date.getFullYear(),
                            month: date.getMonth() + 1,
                            day: date.getDate(),
                            hour: date.getHours(),
                            min: date.getMinutes(),
                            sec: date.getSeconds()
                        }
                    };
                    try {
                        await Api.postUpdate(body);
                    } catch (error) {
                        console.error('Failed to set date/time', error);
                    }
                });
            }

            /* datetime */
            let datetime = document.querySelectorAll(".datetime");
            if (datetime.length > 0) {
                window.setInterval(() => {
                    let date = new Date()
                    datetime.forEach((element) => {
                        element.innerHTML = date.toLocaleDateString('en-us', {
                            month: 'long',
                            day: 'numeric',
                            year: 'numeric'
                        });
                        element.innerHTML += " ";
                        element.innerHTML += date.toLocaleTimeString('en-us', {
                            hour12: false
                        })
                    });
                }, 1000);
            }

            /* test api */
            document.querySelector("button[name='weatherapi_save']").addEventListener('click', async event => {
                let msgElement = document.querySelector(".weatherapi_save");
                msgElement.classList.remove('error');
                msgElement.innerHTML = "";
                const settings = await SettingsStore.load();
                const hasOutdoorWeather = settings.HAS_ONLINEWEATHER || settings.HAS_USWEATHER;
                const requiresApiKey = settings.HAS_ONLINEWEATHER;

                if (!hasOutdoorWeather) {
                    msgElement.classList.add('error');
                    msgElement.innerHTML = "Outdoor weather is disabled";
                    return;
                }

                let apikey = document.querySelector("input[name='weatherapi_apikey']").value;
                if (requiresApiKey && apikey.trim().length === 0) {
                    msgElement.classList.add('error');
                    msgElement.innerHTML = "Missing Api Key";
                    return;
                }

                let lat = document.querySelector("input[name='weatherapi_latitude']").value;
                if (lat.trim().length === 0) {
                    msgElement.classList.add('error');
                    msgElement.innerHTML = "Missing Latitude (Press Get Location Button)";
                    return;
                }

                let long = document.querySelector("input[name='weatherapi_longitude']").value;
                if (long.trim().length === 0) {
                    msgElement.classList.add('error');
                    msgElement.innerHTML = "Missing Longitude (Press Get Location Button)";
                    return;
                }

                let units = (document.querySelector("input[name='temperatureSymbol']:checked").value === "36") ? "metric" : "imperial";
                let unit = (units === "metric") ? "C" : "F";
                let weatherapiUpdate = {
                    latitude: lat,
                    longitude: long
                };

                if (requiresApiKey) {
                    weatherapiUpdate.apikey = apikey;
                    let apiUrl = `http://api.openweathermap.org/data/2.5/weather?lat=${lat}&lon=${long}&APPID=${apikey}&units=${units}`;
                    msgElement.innerHTML = "Fetching temp...";
                    let response = await fetch(apiUrl);
                    if (!response.ok) {
                        let error = await response.json();
                        msgElement.innerHTML = error.message;
                        msgElement.classList.add('error');
                        console.error(response);
                        return;
                    }

                    let data = await response.json();
                    msgElement.innerHTML = `${data['main']['temp']}&deg;${unit} and ${data['main']['humidity']}% humidity`;
                } else {
                    msgElement.innerHTML = "Saving location...";
                }

                try {
                    await SettingsStore.updateNested('weatherapi', weatherapiUpdate);
                    localStorage.setItem("weatherapi", JSON.stringify(weatherapiUpdate));
                } catch (error) {
                    console.error('Failed to save weather API settings', error);
                    msgElement.classList.add('error');
                    msgElement.innerHTML = 'Failed to save settings';
                    return;
                }

                msgElement.innerHTML += " Saved!!!";

                document.querySelectorAll("[name='temperature_outdoor_enable'], [name='humidity_outdoor_enable']").forEach(element => {
                    element.removeAttribute('disabled');
                });
                setElementsHidden(".weathercheckbox", true);
                setElementsHidden(".weather-dependent", !settings.HAS_USWEATHER);
            });

            document.querySelector("[name='clearapikey']").addEventListener('click', async event => {
                try {
                    await SettingsStore.updateNested('weatherapi', {
                        apikey: '',
                        latitude: '',
                        longitude: ''
                    });
                } catch (error) {
                    console.error('Failed to clear weather API settings', error);
                }

                localStorage.removeItem("weatherapi");
                document.querySelector("input[name='weatherapi_apikey']").value = '';
                document.querySelectorAll("[name='temperature_outdoor_enable'], [name='humidity_outdoor_enable']").forEach(element => {
                    element.setAttribute('disabled', true);
                });
                document.querySelectorAll(".weathercheckbox").forEach(element => {
                    element.removeAttribute('hidden');
                });
                setElementsHidden(".weather-dependent", true);
            })




                if (localStorage.getItem("HAS_BUZZER") == "true") { //hide FFT if no sounddetector hardware
                document.querySelector("#upload-button").addEventListener('click', async function() {
                        let upload = await uploadFile();
                        if(upload.error == 0) {
                                alert('File uploaded successfully');
                        } else if(upload.error == 1) {
                                alert('File uploading failed - ' + upload.message);
                        }
                });
                }

        // Check if both latitude and longitude are populated
        if (localStorage.getItem("weatherapi")) {
            // If latitude and longitude are set, show the weather-dependent options
            setElementsHidden(".weather-dependent", false);
        } else {
            // If not set, keep them hidden
            setElementsHidden(".weather-dependent", true);
        }

    })();
    }


    /* Debug Page */
    if (document.querySelectorAll("body.debug").length > 0) {
        loadDebug();
    }
	
	

    /* SCHEDULER STUFF */
    if (document.querySelectorAll("form[name='schedule-settings']").length > 0) {	
	        /* load scheduler */
        loadScheduler();
	}
	
	
	
});


  /* load home */
  async function loadHome() {
    try {
      const home = await Api.getHome();
      if (!home) return;

      const leftInput = document.querySelector("[name='left']");
      const rightInput = document.querySelector("[name='right']");
      if (leftInput) {
        leftInput.value = home.scoreboardLeft;
      }
      if (rightInput) {
        rightInput.value = home.scoreboardRight;
      }

      try {
        await SettingsStore.load();
        const leftHex = SettingsStore.getColor('scoreboardLeft');
        const rightHex = SettingsStore.getColor('scoreboardRight');
        if (leftHex) {
          document.documentElement.style.setProperty('--score-left', leftHex);
        }
        if (rightHex) {
          document.documentElement.style.setProperty('--score-right', rightHex);
        }
      } catch (settingsError) {
        console.warn('Failed to load settings for colors', settingsError);
      }

      if (!home.HAS_SOUNDDETECTOR) {
        hideElementById("FFT");
      }
      if (!home.HAS_ONLINEWEATHER && !home.HAS_USWEATHER && !home.HAS_DHT) {
        hideElementById("NOTEMP5");
        hideElementById("NOTEMP6");
      }
    } catch (error) {
      console.error('Failed to load home data', error);
    }
  }
  

/* load settings */
async function loadSettings() {
    try {
        const settings = await SettingsStore.load();
        if (!settings) {
            return null;
        }

        const hasTemperatureHumidity = settings.HAS_ONLINEWEATHER || settings.HAS_USWEATHER || settings.HAS_DHT;
        const hasOutdoorWeather = settings.HAS_ONLINEWEATHER || settings.HAS_USWEATHER;

        if (!settings.HAS_SOUNDDETECTOR) {
            hideElementById("FFT");
            hideElementById("FFT2");
        }
        if (!settings.HAS_BUZZER) {
            hideElementById("BUZZER");
            hideElementById("BUZZER2");
            localStorage.removeItem("HAS_BUZZER");
        }
        if (!hasTemperatureHumidity) {
            hideElementById("NOTEMP");
            hideElementById("NOTEMP2");
            hideElementById("NOTEMP4");
        }
        if (!hasOutdoorWeather) {
            hideElementById("NOTEMP8");
            hideElementById("NOTEMP9");
            hideElementById("ONLINEWEATHER");
        }
        if (!settings.HAS_USWEATHER) {
            hideElementById("USWEATHER");
            hideElementById("NOTEMP7");
            hideElementById("USWEATHER_SCROLL");
        }
        if (!settings.HAS_ONLINEWEATHER) {
            hideElementById("ONLINEWEATHER_APIKEY");
        }

        for (let [key, value] of Object.entries(settings)) {
            if (key in radioButtons) {
                setRadioButton(radioButtons[key], value);
            } else if (key in dropDownSelectors) {
                setDropDown(dropDownSelectors[key], value);
            } else if (key in directValueSelectors) {
                const normalized = SettingsStore.getNormalizedValue(key);
                setValue(directValueSelectors[key], normalized);
            } else if (key in checkboxSelectors) {
                setCheckbox(checkboxSelectors[key], value);
            } else if (key === 'weatherapi') {
                setWeatherAPI(key, value);
                if (weatherApiConfigured(settings)) {
                    localStorage.setItem("weatherapi", JSON.stringify(value));
                }
            }
        }

        if (!weatherApiConfigured(settings)) {
            localStorage.removeItem("weatherapi");
        }
        setElementsHidden(".weather-dependent", !(settings.HAS_USWEATHER && weatherApiConfigured(settings)));

        if (settings.HAS_BUZZER) {
            localStorage.setItem("HAS_BUZZER", "true");
            const songSelect = document.querySelector("[name='song']");
            if (songSelect) {
                songSelect.innerHTML = '';
                for (let [key] of Object.entries(settings.listOfSong || {})) {
                    let element = document.createElement("option");
                    element.value = key;
                    element.innerHTML = key;
                    songSelect.appendChild(element);
                }
                if (settings.defaultAudibleAlarm !== undefined) {
                    setDropDown('song', settings.defaultAudibleAlarm);
                }
            }

            const playButton = document.querySelector("[name='play-song']");
            if (playButton) {
                playButton.addEventListener('click', async () => {
                    const song = document.querySelector("[name='song']").value;
                    try {
                        await Api.playSong(song);
                    } catch (error) {
                        console.error('Failed to play song', error);
                    }
                });
            }

            const deleteButton = document.querySelector("[name='delete-song']");
            if (deleteButton) {
                deleteButton.addEventListener('click', async () => {
                    const song = document.querySelector("[name='song']").value;
                    try {
                        await Api.deleteSong(song);
                        location.reload();
                    } catch (error) {
                        console.error('Failed to delete song', error);
                    }
                });
            }
        }

        return settings;
    } catch (error) {
        console.error('Failed to load settings', error);
        throw error;
    }
}

async function loadDebug() {
    let add = document.querySelector(".debug-items");
    if (!add) return;
    try {
        const settings = await Api.getDebug();
        if (!settings) {
            return;
        }
        for (let [key, value] of Object.entries(settings)) {
            let element = document.createElement("tr");
            element.innerHTML = `<td>${key}</td><td>${value}</td>`;
            add.appendChild(element);
        }
    } catch (error) {
        console.error('Failed to load debug information', error);
    }
}


/* load scheduler */
async function loadScheduler() {
    try {
        const data = await Api.getScheduler();
        if (!data) {
            return;
        }

        let add = document.querySelector("[name='song']");
        if (add) {
            add.innerHTML = '';
            for (let [key] of Object.entries(data.listOfSong || {})) {
                let element = document.createElement("option");
                element.value = key;
                element.innerHTML = key;
                add.appendChild(element);
            }
        }

        console.log(data.jsonScheduleData);
    } catch (error) {
        console.error('Failed to load scheduler data', error);
    }
}




/* set form elements */
async function setObjectAPI(objkey, obj) {
    for (let [key, value] of Object.entries(obj)) {
        let element = document.querySelector(`[name='${objkey}_${key}']`);
        if (!element) continue;
        console.log(objkey, obj, `[name='${objkey}_${key}']`);
        element.checked = value
        element.addEventListener('click', async event => {
            try {
                await SettingsStore.updateNested(objkey, { [key]: event.target.checked });
            } catch (error) {
                console.error(`Failed to update ${objkey}.${key}`, error);
            }
        });
    }
}

async function setWeatherAPI(objkey, obj) {
    if (!obj) {
        return;
    }
    let enableOutdoorCheckBoxs = true;
    for (let [key, value] of Object.entries(obj)) {
        const str = value != null ? value.toString() : '';
        enableOutdoorCheckBoxs &= (str.trim().length > 0);
        let input = document.querySelector(`[name='${objkey}_${key}']`);
        if (input) {
            input.value = str;
        }
    }

    if (enableOutdoorCheckBoxs) {
        document.querySelectorAll("[name='temperature_outdoor_enable'], [name='humidity_outdoor_enable']").forEach(element => {
            element.removeAttribute('disabled');
        });

        document.querySelectorAll(".weathercheckbox").forEach(element => {
            element.setAttribute('hidden', true);
        });
    }
}

async function setCheckbox(selectorName, value) {
   document.querySelector(`[name='${selectorName}']`).checked = value;
}

async function setValue(selectorName, value) {
    const el = document.querySelector(`[name='${selectorName}']`);
    if (!el) return;
    // if it's a color picker and we got a number, format as #RRGGBB
    if (el.type === 'color' && typeof value === 'number') {
        el.value = '#' + value.toString(16).padStart(6, '0');
    } else {
        el.value = value;
    }
}

async function setDropDown(selectorName, value) {
    let select = document.querySelector(`select[name='${selectorName}']`);
    [...select.options].forEach((option, index) => {
        if(value.toString() === option.value) {
            select.selectedIndex = index;
        }
    });
}

async function setRadioButton(selectorName, value) {
    document.querySelectorAll(`input[name='${selectorName}']`).forEach((element) => {
        if (element.value === value.toString()) {
            element.checked = true;
        }
    });
}

/* utility functions */
function hexToRgb(hex) {
    // Expand shorthand form (e.g. "03F") to full form (e.g. "0033FF")
    var shorthandRegex = /^#?([a-f\d])([a-f\d])([a-f\d])$/i;
    hex = hex.replace(shorthandRegex, function(m, r, g, b) {
      return r + r + g + g + b + b;
    });
  
    var result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
    return result ? {
      r: parseInt(result[1], 16),
      g: parseInt(result[2], 16),
      b: parseInt(result[3], 16)
    } : null;
}

function debounce(func, wait, immediate) {
    var timeout;
    return function() {
        var context = this, args = arguments;
        var later = function() {
            timeout = null;
            if (!immediate) func.apply(context, args);
        };
        var callNow = immediate && !timeout;
        clearTimeout(timeout);
        timeout = setTimeout(later, wait);
        if (callNow) func.apply(context, args);
    };
};
// async function managing upload operation
async function uploadFile() {
    try {
        const fileInput = document.querySelector("#file-to-upload");
        if (fileInput.files.length === 0) {
            throw new Error('No file selected');
        }

        const data = new FormData();
        data.append('title', 'Sample Title');
        const selectedFile = fileInput.files[0];
        data.append('file', selectedFile);

        const jsonResponse = await Api.uploadSong(data);
        if (jsonResponse && jsonResponse.error === 1) {
            throw new Error(jsonResponse.message);
        }

        const message = jsonResponse && jsonResponse.message ? jsonResponse.message : 'Upload completed';
        return { error: 0, message };
    } catch (error) {
        console.error('Upload failed', error);
        // Fallback: verify if the file is present via settings and treat as success
        try {
            const settings = await Api.getSettings();
            const uploadedName = (document.querySelector("#file-to-upload").files[0] || {}).name;
            if (settings && settings.listOfSong && uploadedName && Object.prototype.hasOwnProperty.call(settings.listOfSong, uploadedName)) {
                return { error: 0, message: 'Upload completed' };
            }
        } catch (probeErr) {
            console.warn('Post-upload verification failed', probeErr);
        }
        return { error: 1, message: error.message };
    }
}
