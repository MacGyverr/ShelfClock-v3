(function (global) {
  const BASE_PATH = '';

  async function request(path, options = {}) {
    const {
      method = 'GET',
      body,
      headers = {},
      expectJson = true
    } = options;

    const fetchOptions = { method, headers: { ...headers } };

    if (body !== undefined) {
      if (body instanceof FormData) {
        fetchOptions.body = body;
      } else {
        fetchOptions.body = JSON.stringify(body);
        if (!fetchOptions.headers['Content-Type']) {
          fetchOptions.headers['Content-Type'] = 'application/json';
        }
      }
    }

    if (!fetchOptions.headers['Accept']) {
      fetchOptions.headers['Accept'] = 'application/json, text/plain, */*';
    }

    let response;
    try {
      response = await fetch(`${BASE_PATH}${path}`, fetchOptions);
    } catch (error) {
      throw new Error(`Network error while calling ${path}: ${error.message}`);
    }

    if (!response.ok) {
      let message = `Request to ${path} failed with ${response.status}`;
      try {
        const data = await response.json();
        if (data && data.message) {
          message = data.message;
        }
      } catch (err) {
        // ignore json parse errors
      }
      throw new Error(message);
    }

    if (!expectJson) {
      return response;
    }

    const contentType = response.headers.get('content-type') || '';
    if (contentType.includes('application/json')) {
      return response.json();
    }

    return null;
  }

  const Api = {
    basePath: BASE_PATH,
    request,
    getSettings() {
      return request('/getsettings');
    },
    getHome() {
      return request('/gethome');
    },
    getDebug() {
      return request('/getdebug');
    },
    getScheduler() {
      return request('/getscheduler');
    },
    postUpdate(body) {
      return request('/updateanything', { method: 'POST', body, expectJson: false });
    },
    playSong(song) {
      return request('/playsong', { method: 'POST', body: { song }, expectJson: false });
    },
    deleteSong(song) {
      return request('/deleteSong', { method: 'POST', body: { song }, expectJson: false });
    },
    uploadSong(formData) {
      return request('/uploadSong', {
        method: 'POST',
        body: formData
      });
    },
    postScheduler(payload) {
      return request('/scheduler', { method: 'POST', body: payload, expectJson: false });
    },
    deleteScheduler(deleteData) {
      return request('/schedulerDelete', {
        method: 'POST',
        body: { deleteData },
        expectJson: false
      });
    }
  };

  global.Api = Api;
})(window);
