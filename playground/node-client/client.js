'use strict'

const fs = require('node:fs')
const { Client } = require('/opt/node-postgres/packages/pg')

const MAX_RESPONSE_BYTES = 64 * 1024
const HTTP_TIMEOUT_MS = 10_000
const DEVICE_GRANT = 'urn:ietf:params:oauth:grant-type:device_code'

function fail(message) {
  throw new Error(message)
}

function decodeJwtPart(part, name) {
  if (typeof part !== 'string' || part.length === 0 || part.length > MAX_RESPONSE_BYTES) {
    fail(`token has an invalid ${name}`)
  }
  try {
    const value = JSON.parse(Buffer.from(part, 'base64url').toString('utf8'))
    if (value === null || Array.isArray(value) || typeof value !== 'object') fail(`token has an invalid ${name}`)
    return value
  } catch {
    fail(`token has an invalid ${name}`)
  }
}

function printTokenInformation(token) {
  const parts = token.split('.')
  if (parts.length !== 3) {
    console.error('OAuth token information: token is not a compact three-part JWT')
    return
  }

  const header = decodeJwtPart(parts[0], 'JWT header')
  const claims = decodeJwtPart(parts[1], 'JWT claims')
  const information = {
    warning: 'decoded token data is unverified and may contain identifying information; the reusable bearer token and signature are not shown',
    header,
    claims,
  }
  console.error(`OAuth token information after connection failure (review before sharing):\n${JSON.stringify(information, null, 2)}`)
}

function readConfig() {
  const paths = [process.env.OAUTH_PLAYGROUND_CONFIG || '/playground/generated/profile.json']
  let contents
  let selectedPath
  for (const path of paths) {
    try {
      contents = fs.readFileSync(path, 'utf8')
      selectedPath = path
      break
    } catch (error) {
      if (error.code !== 'ENOENT') throw error
    }
  }
  if (!selectedPath) fail(`configuration is missing; expected one of: ${paths.join(', ')}`)
  const config = JSON.parse(contents)
  for (const name of [
    'issuer', 'discoveryUri', 'clientId', 'audience', 'scope',
    'mappingMode', 'identityClaim', 'rolesClaim',
  ]) {
    if (typeof config[name] !== 'string' || config[name].length === 0) fail(`invalid or missing ${name}`)
  }
  if (!config.issuer.startsWith('https://') || !config.discoveryUri.startsWith('https://')) {
    fail('issuer and discoveryUri must use HTTPS')
  }
  return config
}

async function readBoundedJson(response, context) {
  const declaredLength = Number(response.headers.get('content-length'))
  if (Number.isFinite(declaredLength) && declaredLength > MAX_RESPONSE_BYTES) {
    fail(`${context} response is too large`)
  }

  const reader = response.body.getReader()
  const chunks = []
  let length = 0
  for (;;) {
    const { done, value } = await reader.read()
    if (done) break
    length += value.length
    if (length > MAX_RESPONSE_BYTES) {
      await reader.cancel()
      fail(`${context} response is too large`)
    }
    chunks.push(value)
  }

  let value
  try {
    value = JSON.parse(Buffer.concat(chunks, length).toString('utf8'))
  } catch {
    fail(`${context} returned invalid JSON`)
  }
  if (value === null || Array.isArray(value) || typeof value !== 'object') fail(`${context} returned invalid JSON`)
  return value
}

async function postForm(url, form, context) {
  const response = await fetch(url, {
    method: 'POST',
    headers: { 'content-type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams(form),
    signal: AbortSignal.timeout(HTTP_TIMEOUT_MS),
  })
  return { response, body: await readBoundedJson(response, context) }
}

async function acquireToken(config) {
  const discoveryResponse = await fetch(config.discoveryUri, { signal: AbortSignal.timeout(HTTP_TIMEOUT_MS) })
  if (!discoveryResponse.ok) fail(`discovery request failed with HTTP ${discoveryResponse.status}`)
  const metadata = await readBoundedJson(discoveryResponse, 'discovery')
  if (metadata.issuer !== config.issuer) fail('discovery issuer does not exactly match the configured issuer')
  for (const name of ['device_authorization_endpoint', 'token_endpoint']) {
    if (typeof metadata[name] !== 'string' || !metadata[name].startsWith('https://')) {
      fail(`discovery metadata has no secure ${name}`)
    }
  }

  const device = await postForm(
    metadata.device_authorization_endpoint,
    { client_id: config.clientId, scope: config.scope, audience: config.audience },
    'device authorization'
  )
  if (!device.response.ok) fail(`device authorization failed: ${device.body.error || `HTTP ${device.response.status}`}`)
  if (typeof device.body.device_code !== 'string' || typeof device.body.user_code !== 'string') {
    fail('device authorization response is incomplete')
  }
  const verificationUri = device.body.verification_uri_complete || device.body.verification_uri
  if (typeof verificationUri !== 'string') fail('device authorization response has no verification URI')

  console.log(`Open this URL to authorize the PostgreSQL connection:\n${verificationUri}`)
  if (!device.body.verification_uri_complete) console.log(`Enter code: ${device.body.user_code}`)

  const expiresIn = Math.min(Math.max(Number(device.body.expires_in) || 600, 1), 900)
  let interval = Math.min(Math.max(Number(device.body.interval) || 5, 1), 30)
  const deadline = Date.now() + expiresIn * 1000
  while (Date.now() < deadline) {
    await new Promise((resolve) => setTimeout(resolve, interval * 1000))
    const token = await postForm(
      metadata.token_endpoint,
      { grant_type: DEVICE_GRANT, device_code: device.body.device_code, client_id: config.clientId },
      'token endpoint'
    )
    if (token.response.ok) {
      if (token.body.token_type !== 'Bearer' || typeof token.body.access_token !== 'string' || !token.body.access_token) {
        fail('token endpoint returned an invalid bearer token response')
      }
      return token.body.access_token
    }
    if (token.body.error === 'authorization_pending') continue
    if (token.body.error === 'slow_down') {
      interval = Math.min(interval + 5, 30)
      continue
    }
    fail(`token request failed: ${token.body.error || `HTTP ${token.response.status}`}`)
  }
  fail('device authorization expired')
}

async function main() {
  const config = readConfig()
  const accessToken = await acquireToken(config)
  const client = new Client({
    host: process.env.PGHOST || 'postgres',
    port: Number(process.env.PGPORT || 5432),
    database: process.env.PGDATABASE || config.postgresDatabase || 'playground',
    user: process.env.PGUSER || config.postgresRole || 'app_reader',
    ssl: { rejectUnauthorized: false },
    oauthBearerToken: accessToken,
    connectionTimeoutMillis: 10_000,
  })

  try {
    await client.connect()
    const result = await client.query('select current_user, current_database()')
    console.table(result.rows)
  } catch (error) {
    try {
      printTokenInformation(accessToken)
    } catch (diagnosticError) {
      console.error(`OAuth token information could not be decoded: ${diagnosticError.message}`)
    }
    throw error
  } finally {
    await client.end().catch(() => {})
  }
}

main().catch((error) => {
  console.error(`oauth-node-client: ${error.message}`)
  process.exitCode = 1
})
