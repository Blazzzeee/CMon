import http from 'k6/http';
import { check, sleep } from 'k6';

export const options = {
  scenarios: {
    constant_rps: {
      executor: 'constant-arrival-rate',
      rate: 10000,          // 10k requests per second
      timeUnit: '1s',
      duration: '60s',     // run for 60 seconds
      preAllocatedVUs: 500, // must be high enough to sustain 10k RPS
      maxVUs: 2000,
    },
  },
  thresholds: {
    http_req_failed: ['rate<0.01'],      // <1% failures
    http_req_duration: ['p(95)<50'],     // 95% under 50ms (local machine)
  },
};

export default function () {
  const url = 'http://0.0.0.0:8000/health';

  const params = {
    headers: {
      'ACCESS_TOKEN': 'dc804bd1f472c23c9a3c0e640aa2d70979639ef5425cffd989f1dcff0272ca96',
    },
    timeout: '2s',
  };

  const res = http.get(url, params);

  check(res, {
    'status is 200': (r) => r.status === 200,
  });
}
