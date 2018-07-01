#include "GameCustomSocket.h"
/*
�������շ������ԭ���Ǵ���socket��ճ������ͼ���socket����
*/


CGameSocket::CGameSocket()
{
	// ���ݽṹ��ʼ��  
	memset(m_bufOutput, 0, sizeof(m_bufOutput));
	memset(m_bufInput, 0, sizeof(m_bufInput));
}

void CGameSocket::closeSocket()
{
	// socket����
	closesocket(m_sockClient);
	WSACleanup();
}

bool CGameSocket::Create(const char* pszServerIP, int nServerPort)
{
	// ������  
	if (pszServerIP == 0 || strlen(pszServerIP) > 15) {
		return false;
	}

	WSADATA wsaData;
	WORD version = MAKEWORD(2, 0);
	int ret = WSAStartup(version, &wsaData);//win sock start up  
	if (ret != 0) {
		return false;
	}

	// �������׽���  
	m_sockClient = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_sockClient == INVALID_SOCKET) {
		closeSocket();
		return false;
	}

	// ����SOCKETΪ������ 
	int optval = 1;
	if (setsockopt(m_sockClient, SOL_SOCKET, SO_KEEPALIVE, (char *)&optval, sizeof(optval)))
	{
		closeSocket();
		return false;
	}

	// ����Ϊ������
	DWORD nMode = 1;
	int nRes = ioctlsocket(m_sockClient, FIONBIO, &nMode);
	if (nRes == SOCKET_ERROR) {
		closeSocket();
		return false;
	}


	unsigned long serveraddr = inet_addr(pszServerIP);
	if (serveraddr == INADDR_NONE)   // ���IP��ַ��ʽ����  
	{
		closeSocket();
		return false;
	}

	sockaddr_in addr_in;
	memset((void *)&addr_in, 0, sizeof(addr_in));
	addr_in.sin_family = AF_INET;
	addr_in.sin_port = htons(nServerPort);
	addr_in.sin_addr.s_addr = serveraddr;

	if (connect(m_sockClient, (sockaddr *)&addr_in, sizeof(addr_in)) == SOCKET_ERROR) {
		if (hasError()) {
			closeSocket();
			return false;
		}
		else    // WSAWOLDBLOCK  
		{
			timeval timeout;
			timeout.tv_sec = 2;
			timeout.tv_usec = 0;
			fd_set writeset, exceptset;
			FD_ZERO(&writeset);
			FD_ZERO(&exceptset);
			FD_SET(m_sockClient, &writeset);
			FD_SET(m_sockClient, &exceptset);

			int ret = select(FD_SETSIZE, NULL, &writeset, &exceptset, &timeout);
			if (ret == 0 || ret < 0) {
				closeSocket();
				return false;
			}
			else  // ret > 0  
			{
				ret = FD_ISSET(m_sockClient, &exceptset);
				if (ret)     // or (!FD_ISSET(m_sockClient, &writeset)  
				{
					closeSocket();
					return false;
				}
			}
		}
	}

	m_nInbufLen = 0;
	m_nInbufStart = 0;
	m_nOutbufLen = 0;

	struct linger so_linger;
	so_linger.l_onoff = 1;
	so_linger.l_linger = 500;
	setsockopt(m_sockClient, SOL_SOCKET, SO_LINGER, (const char*)&so_linger, sizeof(so_linger));

	return true;
}

bool CGameSocket::SendMsg(const char* pBuf, int nSize)
{
	/*
	���������������˷������ݣ����û��棬���ٷ�������Ƶ��ͬʱ�����ǲ��÷���������Ҫ����δ����������ݣ�
	@param pBuf: �����ַ������ڱ�ϵͳ��Լ��Ϊ json��ʽ�����ݣ�ͨ�����л�������ݣ�
	@param nSize: ���ݳ���
	@param bSendRightNow: �Ƿ��������ͣ�Ĭ��Ϊ�������ͣ�������δ��������û��ʵʱ��Ҫ�����Խ����ֶ�����flase���������´η�����һ���ͣ�
	*/

	// ���������ǲ������ģ�������Ҫ������ǰ���ϱ�ʾ���ݳ��ȵ����ݣ��ݶ���ʾ���ݳ��ȵ����ݳ�ΪLENGTHDATASIZE
	nSize = nSize + LENGTHDATASIZE;
	// ���ϳ������ݺ�������
	char* tempBuf = new char[nSize + LENGTHDATASIZE];
	// ���ݳ�������
	char digBuf[LENGTHDATASIZE] = { 0 };

	//���кϷ��Լ�� 
	if (pBuf == 0 || nSize <= 0) {
		return false;
	}

	if (m_sockClient == INVALID_SOCKET) {
		return false;
	}

	// ����������ݳ������ݺ����巢�����ݳ���
	for (int i = 0; i < LENGTHDATASIZE; i++) {
		digBuf[i] = nSize >> i * 8;
	}

	// �����ݳ����������ӵ���ʵ����ǰ��
	memcpy(tempBuf, digBuf, LENGTHDATASIZE);
	memcpy(tempBuf + LENGTHDATASIZE, pBuf, nSize);

	// ���ͨѶ��Ϣ������  
	int packsize = 0;
	packsize = nSize;

	// ������ͳ������ʱ���������ͻ��������ݣ�����շ��ͻ�������
	if (m_nOutbufLen + nSize > OUTBUFSIZE) {
		// ��������OUTBUF�е����ݣ������OUTBUF��  
		Flush();
		// �������ݺ󣬻��������˵��Flush()�����˻����Ƿ��ͻ���̫С
		if (m_nOutbufLen + nSize > OUTBUFSIZE) {
			Destroy();
			return false;
		}
	}
	// �������ӵ�BUFβ  
	memcpy(m_bufOutput + m_nOutbufLen, tempBuf, nSize);
	m_nOutbufLen += nSize;

	// ɾ�����������ַ�ָ�룬�����ڴ�й¶
	delete[] tempBuf;

	return true;
}

bool CGameSocket::ReceiveMsg(char* pBuf, int& nSize)
{
	int packsize = 0;
	//������  
	if (pBuf == NULL || nSize <= 0) {
		return false;
	}
	//������  
	if (m_sockClient == INVALID_SOCKET) {
		return false;
	}
	// ����Ƿ���һ����Ϣ(С��LENGTHDATASIZE���޷���ȡ����Ϣ����)  
	if (m_nInbufLen < LENGTHDATASIZE) {
		//  ���û������ɹ�  ����   ���û��������ֱ�ӷ���  
		if (!recvFromSock() || m_nInbufLen < LENGTHDATASIZE) {
			return false;
		}
	}

	// ����Щ�ΰ�����
	for (int i = 0; i < LENGTHDATASIZE; i++) {
		packsize += (unsigned char)m_bufInput[m_nInbufStart + i] * pow(256, i);
	}

	// �����Ϣ���ߴ���� �ݶ����16k ����������Ϊ���󣬶Ի��������գ����¿�ʼ��
	if (packsize <= 0 || packsize > _MAX_MSGSIZE) {
		m_nInbufLen = 0;
		m_nInbufStart = 0;
		return false;
	}

	// �����Ϣ�Ƿ�����(�����Ҫ��������Ϣ���ڴ�ʱ���������ݳ��ȣ���Ҫ�ٴ��������ʣ������)  
	if (packsize > m_nInbufLen) {
		// �ٴγ��Ի�ȡһ����������Ϣ�����ʧ�ܻ�����Ȼ����������С���򷵻أ�����һ֡�ٳ��Ի�ȡ 
		if (!recvFromSock() || packsize > m_nInbufLen) {
			return false;
		}
	}

	// ���Ƴ�һ����Ϣ  
	if (m_nInbufStart + packsize > INBUFSIZE) {
		// ���һ����Ϣ�лػ����󣬼�����������ڻ��λ�������ͷβ

		// �ȿ������λ�����ĩβ�����ݣ�ȥ����Ϣ�����ȣ�ֻ��ȡ���������ݰ���
		int copylen = INBUFSIZE - m_nInbufStart;
		memcpy(pBuf, m_bufInput + m_nInbufStart + LENGTHDATASIZE, copylen - LENGTHDATASIZE);

		// �ٿ������λ�����ͷ����ʣ�ಿ��  
		memcpy((unsigned char *)pBuf + copylen, m_bufInput, packsize - copylen);
		nSize = packsize;
	}
	else {
		// ��Ϣû�лػ�������һ�ο�����ȥ��ȥ����Ϣ�����ȣ�ֻ��ȡ���������ݰ���
		memcpy(pBuf, m_bufInput + m_nInbufStart + LENGTHDATASIZE, packsize - LENGTHDATASIZE);
		nSize = packsize;
	}

	// ���¼��㻷�λ�����ͷ��λ��  
	m_nInbufStart = (m_nInbufStart + packsize) % INBUFSIZE;
	m_nInbufLen -= packsize;
	return  true;
}

bool CGameSocket::hasError()
{
	int err = WSAGetLastError();
	if (err != WSAEWOULDBLOCK) {

		return true;
	}

	return false;
}

// �������ж�ȡ�����ܶ�����ݣ�ʵ��ȡ���������������ݵĵط�  
bool CGameSocket::recvFromSock(void)
{
	if (m_nInbufLen >= INBUFSIZE || m_sockClient == INVALID_SOCKET) {
		return false;
	}

	// ���յ�һ������  
	int savelen, savepos;           // ����Ҫ����ĳ��Ⱥ�λ��  
	if (m_nInbufStart + m_nInbufLen < INBUFSIZE) {
		savelen = INBUFSIZE - (m_nInbufStart + m_nInbufLen);
	}
	else {
		savelen = INBUFSIZE - m_nInbufLen;
	}

	// ���������ݵ�ĩβ  
	savepos = (m_nInbufStart + m_nInbufLen) % INBUFSIZE;
	if (savepos + savelen > INBUFSIZE)
		return 0;

	int inlen = recv(m_sockClient, m_bufInput + savepos, savelen, 0);
	if (inlen > 0) {
		// �н��յ�����  
		m_nInbufLen += inlen;

		if (m_nInbufLen > INBUFSIZE) {
			return false;
		}

		// ���յڶ�������(һ�ν���û����ɣ����յڶ�������)  
		if (inlen == savelen && m_nInbufLen < INBUFSIZE) {
			int savelen = INBUFSIZE - m_nInbufLen;
			int savepos = (m_nInbufStart + m_nInbufLen) % INBUFSIZE;
			if (savepos + savelen > INBUFSIZE)
				return 0;

			inlen = recv(m_sockClient, m_bufInput + savepos, savelen, 0);
			if (inlen > 0) {
				m_nInbufLen += inlen;
				if (m_nInbufLen > INBUFSIZE) {
					return false;
				}
			}
			else if (inlen == 0) {
				Destroy();
				return false;
			}
			else {
				// �����ѶϿ����ߴ��󣨰���������  
				if (hasError()) {
					Destroy();
					return false;
				}
			}
		}
	}
	else if (inlen == 0) {
		Destroy();
		return false;
	}
	else {
		// �����ѶϿ����ߴ��󣨰���������  
		if (hasError()) {
			Destroy();
			return false;
		}
	}
	return true;
}

bool CGameSocket::Flush(void)
{
	/*
	����������
	*/
	if (m_sockClient == INVALID_SOCKET) {
		return false;
	}

	if (m_nOutbufLen <= 0) {
		return true;
	}

	// ����һ������  
	int outsize;
	outsize = send(m_sockClient, m_bufOutput, m_nOutbufLen, 0);
	if (outsize > 0) {
		// ɾ���ѷ��͵Ĳ���  
		if (m_nOutbufLen - outsize > 0) {
			memcpy(m_bufOutput, m_bufOutput + outsize, m_nOutbufLen - outsize);
		}

		m_nOutbufLen -= outsize;

		if (m_nOutbufLen < 0) {
			return false;
		}
	}
	else {
		if (hasError()) {
			Destroy();
			return false;
		}
	}

	return true;
}

bool CGameSocket::Check(void)
{
	// ���״̬  
	if (m_sockClient == INVALID_SOCKET) {
		return false;
	}

	char buf[1];

	// �鿴���ջ�����������
	int ret = recv(m_sockClient, buf, 1, MSG_PEEK);
	if (ret == 0) {
		Destroy();
		return false;
	}
	else if (ret < 0) {
		if (hasError()) {
			Destroy();
			return false;
		}
		else {    // ����  
			return true;
		}
	}
	else {    // ������  
		return true;
	}

	return true;
}

void CGameSocket::Destroy(void)
{
	// �ر�  
	struct linger so_linger;
	so_linger.l_onoff = 1;
	so_linger.l_linger = 500;
	int ret = setsockopt(m_sockClient, SOL_SOCKET, SO_LINGER, (const char*)&so_linger, sizeof(so_linger));

	closeSocket();

	m_sockClient = INVALID_SOCKET;
	m_nInbufLen = 0;
	m_nInbufStart = 0;
	m_nOutbufLen = 0;

	memset(m_bufOutput, 0, sizeof(m_bufOutput));
	memset(m_bufInput, 0, sizeof(m_bufInput));
}






