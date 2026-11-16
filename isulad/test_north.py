"""Protocol smoke tests inside north container, with TLS/SSH verification enabled."""
import json,math,ssl,time,urllib.request,urllib.error
import management as m
from pathlib import Path
import grpc,paramiko
from lxml import etree as E
import gnmi_pb2 as pb
import gnmi_pb2_grpc as rpc
from wait_ready import wait
wait()
p=Path('/lab-state/security');secret=json.loads((p/'credentials.json').read_text());results=[]
def ok(name):results.append({'test':name,'passed':True});print('PASS '+name,flush=True)
ctx=ssl.create_default_context(cafile=str(p/'server.crt'))
req=urllib.request.Request('https://127.0.0.1:8443/v1/platform',headers={'Authorization':'Bearer '+secret['token']})
with urllib.request.urlopen(req,context=ctx,timeout=10) as f:assert any(k.startswith('PSU_INFO|') for k in json.load(f))
ok('REST authenticated native platform state')
try:urllib.request.urlopen('https://127.0.0.1:8443/v1/platform',context=ctx,timeout=10);raise AssertionError('authentication bypass')
except urllib.error.HTTPError as e:assert e.code==401
ok('REST rejects unauthenticated request')
with grpc.secure_channel('127.0.0.1:9339',grpc.ssl_channel_credentials((p/'server.crt').read_bytes())) as channel:
 stub=rpc.gNMIStub(channel);md=(('authorization','Bearer '+secret['token']),)
 assert stub.Capabilities(pb.CapabilityRequest(),metadata=md,timeout=10).supported_models[0].name=='sonic-otn-system'
 path=pb.Path(elem=[pb.PathElem(name='platform')])
 out=stub.Get(pb.GetRequest(path=[path],encoding=pb.JSON),metadata=md,timeout=10)
 assert any(k.startswith('PSU_INFO|') for k in json.loads(out.notification[0].update[0].val.json_val))
 ok('gNMI TLS Get native platform state')
 sub=pb.SubscribeRequest(subscribe=pb.SubscriptionList(mode=pb.SubscriptionList.ONCE,encoding=pb.JSON,subscription=[pb.Subscription(path=path)]))
 stream=list(stub.Subscribe(iter([sub]),metadata=md,timeout=10));assert stream[-1].sync_response and stream[0].HasField('update')
 ok('gNMI Subscribe ONCE and sync')
 try:stub.Capabilities(pb.CapabilityRequest(),timeout=10);raise AssertionError('authentication bypass')
 except grpc.RpcError as e:assert e.code()==grpc.StatusCode.UNAUTHENTICATED
 ok('gNMI rejects unauthenticated request')
client=paramiko.SSHClient();key=paramiko.RSAKey.from_private_key_file(str(p/'ssh_host_key'))
client.get_host_keys().add('[127.0.0.1]:8300',key.get_name(),key)
client.connect('127.0.0.1',port=8300,username=secret['user'],password=secret['password'],look_for_keys=False,allow_agent=False,timeout=10)
ch=client.get_transport().open_session();ch.settimeout(10);ch.invoke_subsystem('netconf');end=b']]>]]>';buf=b''
def receive():
 global buf
 while end not in buf:
  chunk=ch.recv(65536)
  if not chunk:raise EOFError
  buf+=chunk
 packet,buf=buf.split(end,1);return E.fromstring(packet)
ns='urn:ietf:params:xml:ns:netconf:base:1.0'
assert E.QName(receive()).localname=='hello'
ch.sendall(('<hello xmlns="'+ns+'"><capabilities><capability>urn:ietf:params:netconf:base:1.0</capability></capabilities></hello>').encode()+end)
ch.sendall(('<rpc xmlns="'+ns+'" message-id="1"><get><filter type="subtree"><platform xmlns="urn:sonic-otn:integration:2026-11-10"/></filter></get></rpc>').encode()+end)
reply=receive();assert reply.get('message-id')=='1' and b'PSU_INFO' in E.tostring(reply)
ok('NETCONF SSH hello/get native platform state')
ch.close();client.close()
import asyncio
from pysnmp.hlapi.asyncio import (SnmpEngine,UsmUserData,UdpTransportTarget,ContextData,ObjectType,ObjectIdentity,get_cmd,usmHMAC192SHA256AuthProtocol,usmAesCfb128Protocol)
async def snmp_test():
 engine=SnmpEngine()
 try:
  auth=UsmUserData(secret['snmp_user'],secret['snmp_auth'],secret['snmp_priv'],authProtocol=usmHMAC192SHA256AuthProtocol,privProtocol=usmAesCfb128Protocol)
  target=await UdpTransportTarget.create(('127.0.0.1',1161),timeout=3,retries=1)
  # Sensor index 1 follows the same installed inventory order as snmp_server.py.
  name=next(iter(json.loads(Path('/system/platform-default.json').read_text())['devices']['thermals']))
  expected_c=float(json.loads(Path('/lab-state/platform.json').read_text())['devices']['thermals'][name]['temperature'])
  assert math.isfinite(expected_c),f'invalid virtual input temperature: {expected_c}'
  expected=int(round(expected_c*100));deadline=time.monotonic()+120
  while True:
   error,status,index,values=await get_cmd(engine,auth,target,ContextData(),ObjectType(ObjectIdentity('1.3.6.1.2.1.1.1.0')),ObjectType(ObjectIdentity('1.3.6.1.2.1.99.1.1.1.4.1')),ObjectType(ObjectIdentity('1.3.6.1.2.1.99.1.1.1.5.1')))
   assert not error and not status, f'SNMP request failed: error={error}, status={status}, index={index}'
   assert 'virtual hardware' in str(values[0][1]),f'unexpected sysDescr: {values[0][1]}'
   actual=int(values[1][1]);sensor_status=int(values[2][1])
   native=m.db(6).hget('TEMPERATURE_INFO|'+name,'temperature')
   try:native_scaled=int(round(float(native)*100))
   except (TypeError,ValueError,OverflowError):native_scaled=None
   if sensor_status==1 and actual==expected and native_scaled==expected:break
   assert time.monotonic()<deadline, f'temperature convergence timeout: sensor={name}, input_C={expected_c}, STATE_DB_C={native}, SNMP_scaled={actual}, expected_scaled={expected}, sensor_status={sensor_status}'
   await asyncio.sleep(1)
 finally:engine.close_dispatcher()
asyncio.run(snmp_test());ok('SNMPv3 SHA256/AES native temperature')
Path('/lab-state/north-test.json').write_text(json.dumps(results,indent=2))
