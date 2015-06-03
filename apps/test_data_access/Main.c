#include "Main.h"

struct packet {
	int version;
	int ihl;
	int dscp;
	int ecn;
	int total_length;
	int packet_id;
	int fragment_offset;
	int time_to_live;
	int next_proto_id;
	int hdr_checksum;
	long src_addr;
	long dst_addr;
} __attribute__((packed)) packet_default = {1,2,3,4,5,6,7,8,9,10,11,12};

JNIEXPORT void JNICALL Java_Main_setData(JNIEnv *env, jclass class, jobject packet) {
	struct packet p;

	jclass cls = (*env)->FindClass(env, "ObjectPacket");
	jmethodID method1 = (*env)->GetMethodID(env, cls, "getVersion", "()I");
	jmethodID method2 = (*env)->GetMethodID(env, cls, "getIhl", "()I");
	jmethodID method3 = (*env)->GetMethodID(env, cls, "getDscp", "()I");
	jmethodID method4 = (*env)->GetMethodID(env, cls, "getEcn", "()I");
	jmethodID method5 = (*env)->GetMethodID(env, cls, "getTotal_length", "()I");
	jmethodID method6 = (*env)->GetMethodID(env, cls, "getPacket_id", "()I");
	jmethodID method7 = (*env)->GetMethodID(env, cls, "getFragment_offset", "()I");
	jmethodID method8 = (*env)->GetMethodID(env, cls, "getTime_to_live", "()I");
	jmethodID method9 = (*env)->GetMethodID(env, cls, "getNext_proto_id", "()I");
	jmethodID method10 = (*env)->GetMethodID(env, cls, "getHdr_checksum", "()I");
	jmethodID method11 = (*env)->GetMethodID(env, cls, "getSrc_addr", "()J");
	jmethodID method12 = (*env)->GetMethodID(env, cls, "getDst_addr", "()J");

	p.version = (*env)->CallIntMethod(env, packet, method1);
	p.ihl = (*env)->CallIntMethod(env, packet, method2);
	p.dscp = (*env)->CallIntMethod(env, packet, method3);
	p.ecn = (*env)->CallIntMethod(env, packet, method4);
	p.total_length = (*env)->CallIntMethod(env, packet, method5);
	p.packet_id = (*env)->CallIntMethod(env, packet, method6);
	p.fragment_offset = (*env)->CallIntMethod(env, packet, method7);
	p.time_to_live = (*env)->CallIntMethod(env, packet, method8);
	p.next_proto_id = (*env)->CallIntMethod(env, packet, method9);
	p.hdr_checksum = (*env)->CallIntMethod(env, packet, method10);
	p.src_addr = (*env)->CallLongMethod(env, packet, method11);
	p.dst_addr = (*env)->CallLongMethod(env, packet, method12);
}

JNIEXPORT void JNICALL Java_Main_getData__LObjectPacket_2(JNIEnv *env, jclass class, jobject packet) {
	struct packet p = packet_default;

	jclass cls = (*env)->FindClass(env, "ObjectPacket");
	jmethodID method1 = (*env)->GetMethodID(env, cls, "setVersion", "(I)V");
	jmethodID method2 = (*env)->GetMethodID(env, cls, "setIhl", "(I)V");
	jmethodID method3 = (*env)->GetMethodID(env, cls, "setDscp", "(I)V");
	jmethodID method4 = (*env)->GetMethodID(env, cls, "setEcn", "(I)V");
	jmethodID method5 = (*env)->GetMethodID(env, cls, "setTotal_length", "(I)V");
	jmethodID method6 = (*env)->GetMethodID(env, cls, "setPacket_id", "(I)V");
	jmethodID method7 = (*env)->GetMethodID(env, cls, "setFragment_offset", "(I)V");
	jmethodID method8 = (*env)->GetMethodID(env, cls, "setTime_to_live", "(I)V");
	jmethodID method9 = (*env)->GetMethodID(env, cls, "setNext_proto_id", "(I)V");
	jmethodID method10 = (*env)->GetMethodID(env, cls, "setHdr_checksum", "(I)V");
	jmethodID method11 = (*env)->GetMethodID(env, cls, "setSrc_addr", "(J)V");
	jmethodID method12 = (*env)->GetMethodID(env, cls, "setDst_addr", "(J)V");

	(*env)->CallVoidMethod(env, packet, method1, p.version);
	(*env)->CallVoidMethod(env, packet, method2, p.ihl);
	(*env)->CallVoidMethod(env, packet, method3, p.dscp);
	(*env)->CallVoidMethod(env, packet, method4, p.ecn);
	(*env)->CallVoidMethod(env, packet, method5, p.total_length);
	(*env)->CallVoidMethod(env, packet, method6, p.packet_id);
	(*env)->CallVoidMethod(env, packet, method7, p.fragment_offset);
	(*env)->CallVoidMethod(env, packet, method8, p.time_to_live);
	(*env)->CallVoidMethod(env, packet, method9, p.next_proto_id);
	(*env)->CallVoidMethod(env, packet, method10, p.hdr_checksum);
	(*env)->CallVoidMethod(env, packet, method11, p.src_addr);
	(*env)->CallVoidMethod(env, packet, method12, p.dst_addr);
}

JNIEXPORT void JNICALL Java_Main_getData__Ljava_nio_ByteBuffer_2(JNIEnv *env, jclass class, jobject buffer) {
	char *buf = (char*)(*env)->GetDirectBufferAddress(env, buffer);
	struct packet p = packet_default;
	buf[0] = p.version;
	buf[4] = p.ihl;
	buf[8] = p.dscp;
	buf[12] = p.ecn;
	buf[16] = p.total_length;
	buf[20] = p.packet_id;
	buf[24] = p.fragment_offset;
	buf[28] = p.time_to_live;
	buf[32] = p.next_proto_id;
	buf[36] = p.hdr_checksum;
	buf[40] = p.src_addr;
	buf[48] = p.dst_addr;
}

JNIEXPORT jlong JNICALL Java_Main_getPointer(JNIEnv *env, jclass class) {
	struct packet p = packet_default;
	return (long)&p;
}


