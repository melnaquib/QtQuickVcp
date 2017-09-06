/****************************************************************************
**
** This file was generated by a code generator based on imatix/gsl
** Any changes in this file will be lost.
**
****************************************************************************/
#include "rpcclient.h"
#include <google/protobuf/text_format.h>
#include "debughelper.h"

#if defined(Q_OS_IOS)
namespace gpb = google_public::protobuf;
#else
namespace gpb = google::protobuf;
#endif

using namespace nzmqt;

namespace machinetalk { namespace common {

/** Generic RPC Client implementation */
RpcClient::RpcClient(QObject *parent)
    : QObject(parent)
    , m_ready(false)
    , m_debugName("RPC Client")
    , m_socketUri("")
    , m_context(nullptr)
    , m_socket(nullptr)
    , m_state(State::Down)
    , m_previousState(State::Down)
    , m_errorString("")
    , m_heartbeatInterval(2500)
    , m_heartbeatLiveness(0)
    , m_heartbeatResetLiveness(5)
{

    m_heartbeatTimer.setSingleShot(true);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &RpcClient::heartbeatTimerTick);
    // state machine
    connect(this, &RpcClient::fsmDownStart,
            this, &RpcClient::fsmDownStartEvent);
    connect(this, &RpcClient::fsmTryingAnyMsgReceived,
            this, &RpcClient::fsmTryingAnyMsgReceivedEvent);
    connect(this, &RpcClient::fsmTryingHeartbeatTimeout,
            this, &RpcClient::fsmTryingHeartbeatTimeoutEvent);
    connect(this, &RpcClient::fsmTryingHeartbeatTick,
            this, &RpcClient::fsmTryingHeartbeatTickEvent);
    connect(this, &RpcClient::fsmTryingAnyMsgSent,
            this, &RpcClient::fsmTryingAnyMsgSentEvent);
    connect(this, &RpcClient::fsmTryingStop,
            this, &RpcClient::fsmTryingStopEvent);
    connect(this, &RpcClient::fsmUpHeartbeatTimeout,
            this, &RpcClient::fsmUpHeartbeatTimeoutEvent);
    connect(this, &RpcClient::fsmUpHeartbeatTick,
            this, &RpcClient::fsmUpHeartbeatTickEvent);
    connect(this, &RpcClient::fsmUpAnyMsgReceived,
            this, &RpcClient::fsmUpAnyMsgReceivedEvent);
    connect(this, &RpcClient::fsmUpAnyMsgSent,
            this, &RpcClient::fsmUpAnyMsgSentEvent);
    connect(this, &RpcClient::fsmUpStop,
            this, &RpcClient::fsmUpStopEvent);

    m_context = new PollingZMQContext(this, 1);
    connect(m_context, &PollingZMQContext::pollError,
            this, &RpcClient::socketError);
    m_context->start();
}

RpcClient::~RpcClient()
{
    stopSocket();

    if (m_context != nullptr)
    {
        m_context->stop();
        m_context->deleteLater();
        m_context = nullptr;
    }
}

/** Connects the 0MQ sockets */
bool RpcClient::startSocket()
{
    m_socket = m_context->createSocket(ZMQSocket::TYP_DEALER, this);
    m_socket->setLinger(0);

    try {
        m_socket->connectTo(m_socketUri);
    }
    catch (const zmq::error_t &e) {
        QString errorString;
        errorString = QString("Error %1: ").arg(e.num()) + QString(e.what());
        qCritical() << m_debugName << ":" << errorString;
        return false;
    }

    connect(m_socket, &ZMQSocket::messageReceived,
            this, &RpcClient::processSocketMessage);


#ifdef QT_DEBUG
    DEBUG_TAG(1, m_debugName, "sockets connected" << m_socketUri);
#endif

    return true;
}

/** Disconnects the 0MQ sockets */
void RpcClient::stopSocket()
{
    if (m_socket != nullptr)
    {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void RpcClient::resetHeartbeatLiveness()
{
    m_heartbeatLiveness = m_heartbeatResetLiveness;
}

void RpcClient::resetHeartbeatTimer()
{
    if (m_heartbeatTimer.isActive())
    {
        m_heartbeatTimer.stop();
    }

    if (m_heartbeatInterval > 0)
    {
        m_heartbeatTimer.setInterval(m_heartbeatInterval);
        m_heartbeatTimer.start();
    }
}

void RpcClient::startHeartbeatTimer()
{
    resetHeartbeatTimer();
}

void RpcClient::stopHeartbeatTimer()
{
    m_heartbeatTimer.stop();
}

void RpcClient::heartbeatTimerTick()
{
    m_heartbeatLiveness -= 1;
    if (m_heartbeatLiveness == 0)
    {
         if (m_state == State::Up)
         {
             emit fsmUpHeartbeatTimeout(QPrivateSignal());
         }
         else if (m_state == State::Trying)
         {
             emit fsmTryingHeartbeatTimeout(QPrivateSignal());
         }
         return;
    }
    if (m_state == State::Up)
    {
        emit fsmUpHeartbeatTick(QPrivateSignal());
    }
    else if (m_state == State::Trying)
    {
        emit fsmTryingHeartbeatTick(QPrivateSignal());
    }
}

/** Processes all message received on socket */
void RpcClient::processSocketMessage(const QList<QByteArray> &messageList)
{
    Container &rx = m_socketRx;
    rx.ParseFromArray(messageList.last().data(), messageList.last().size());

#ifdef QT_DEBUG
    std::string s;
    gpb::TextFormat::PrintToString(rx, &s);
    DEBUG_TAG(3, m_debugName, "server message" << QString::fromStdString(s));
#endif

    // react to any incoming message

    if (m_state == State::Trying)
    {
        emit fsmTryingAnyMsgReceived(QPrivateSignal());
    }

    else if (m_state == State::Up)
    {
        emit fsmUpAnyMsgReceived(QPrivateSignal());
    }

    // react to ping acknowledge message
    if (rx.type() == MT_PING_ACKNOWLEDGE)
    {
        return; // ping acknowledge is uninteresting
    }

    emit socketMessageReceived(rx);
}

void RpcClient::sendSocketMessage(ContainerType type, Container &tx)
{
    if (m_socket == nullptr) {  // disallow sending messages when not connected
        return;
    }

    tx.set_type(type);
#ifdef QT_DEBUG
    std::string s;
    gpb::TextFormat::PrintToString(tx, &s);
    DEBUG_TAG(3, m_debugName, "sent message" << QString::fromStdString(s));
#endif
    try {
        m_socket->sendMessage(QByteArray(tx.SerializeAsString().c_str(), tx.ByteSize()));
    }
    catch (const zmq::error_t &e) {
        QString errorString;
        errorString = QString("Error %1: ").arg(e.num()) + QString(e.what());
        return;
    }
    tx.Clear();

    if (m_state == State::Up)
    {
        emit fsmUpAnyMsgSent(QPrivateSignal());
    }

    else if (m_state == State::Trying)
    {
        emit fsmTryingAnyMsgSent(QPrivateSignal());
    }
}

void RpcClient::sendPing()
{
    Container &tx = m_socketTx;
    sendSocketMessage(MT_PING, tx);
}

void RpcClient::socketError(int errorNum, const QString &errorMsg)
{
    QString errorString;
    errorString = QString("Error %1: ").arg(errorNum) + errorMsg;
}

void RpcClient::fsmDown()
{
#ifdef QT_DEBUG
    DEBUG_TAG(1, m_debugName, "State DOWN");
#endif
    m_state = State::Down;
    emit stateChanged(m_state);
}

void RpcClient::fsmDownStartEvent()
{
    if (m_state == State::Down)
    {
#ifdef QT_DEBUG
        DEBUG_TAG(1, m_debugName, "Event START");
#endif
        // handle state change
        emit fsmDownExited(QPrivateSignal());
        fsmTrying();
        emit fsmTryingEntered(QPrivateSignal());
        // execute actions
        startSocket();
        resetHeartbeatLiveness();
        sendPing();
        startHeartbeatTimer();
     }
}

void RpcClient::fsmTrying()
{
#ifdef QT_DEBUG
    DEBUG_TAG(1, m_debugName, "State TRYING");
#endif
    m_state = State::Trying;
    emit stateChanged(m_state);
}

void RpcClient::fsmTryingAnyMsgReceivedEvent()
{
    if (m_state == State::Trying)
    {
#ifdef QT_DEBUG
        DEBUG_TAG(1, m_debugName, "Event ANY MSG RECEIVED");
#endif
        // handle state change
        emit fsmTryingExited(QPrivateSignal());
        fsmUp();
        emit fsmUpEntered(QPrivateSignal());
        // execute actions
        resetHeartbeatLiveness();
        resetHeartbeatTimer();
     }
}

void RpcClient::fsmTryingHeartbeatTimeoutEvent()
{
    if (m_state == State::Trying)
    {
#ifdef QT_DEBUG
        DEBUG_TAG(1, m_debugName, "Event HEARTBEAT TIMEOUT");
#endif
        // execute actions
        stopSocket();
        startSocket();
        resetHeartbeatLiveness();
        sendPing();
     }
}

void RpcClient::fsmTryingHeartbeatTickEvent()
{
    if (m_state == State::Trying)
    {
#ifdef QT_DEBUG
        DEBUG_TAG(1, m_debugName, "Event HEARTBEAT TICK");
#endif
        // execute actions
        sendPing();
     }
}

void RpcClient::fsmTryingAnyMsgSentEvent()
{
    if (m_state == State::Trying)
    {
#ifdef QT_DEBUG
        DEBUG_TAG(1, m_debugName, "Event ANY MSG SENT");
#endif
        // execute actions
        resetHeartbeatTimer();
     }
}

void RpcClient::fsmTryingStopEvent()
{
    if (m_state == State::Trying)
    {
#ifdef QT_DEBUG
        DEBUG_TAG(1, m_debugName, "Event STOP");
#endif
        // handle state change
        emit fsmTryingExited(QPrivateSignal());
        fsmDown();
        emit fsmDownEntered(QPrivateSignal());
        // execute actions
        stopHeartbeatTimer();
        stopSocket();
     }
}

void RpcClient::fsmUp()
{
#ifdef QT_DEBUG
    DEBUG_TAG(1, m_debugName, "State UP");
#endif
    m_state = State::Up;
    emit stateChanged(m_state);
}

void RpcClient::fsmUpHeartbeatTimeoutEvent()
{
    if (m_state == State::Up)
    {
#ifdef QT_DEBUG
        DEBUG_TAG(1, m_debugName, "Event HEARTBEAT TIMEOUT");
#endif
        // handle state change
        emit fsmUpExited(QPrivateSignal());
        fsmTrying();
        emit fsmTryingEntered(QPrivateSignal());
        // execute actions
        stopSocket();
        startSocket();
        resetHeartbeatLiveness();
        sendPing();
     }
}

void RpcClient::fsmUpHeartbeatTickEvent()
{
    if (m_state == State::Up)
    {
#ifdef QT_DEBUG
        DEBUG_TAG(1, m_debugName, "Event HEARTBEAT TICK");
#endif
        // execute actions
        sendPing();
     }
}

void RpcClient::fsmUpAnyMsgReceivedEvent()
{
    if (m_state == State::Up)
    {
#ifdef QT_DEBUG
        DEBUG_TAG(1, m_debugName, "Event ANY MSG RECEIVED");
#endif
        // execute actions
        resetHeartbeatLiveness();
     }
}

void RpcClient::fsmUpAnyMsgSentEvent()
{
    if (m_state == State::Up)
    {
#ifdef QT_DEBUG
        DEBUG_TAG(1, m_debugName, "Event ANY MSG SENT");
#endif
        // execute actions
        resetHeartbeatTimer();
     }
}

void RpcClient::fsmUpStopEvent()
{
    if (m_state == State::Up)
    {
#ifdef QT_DEBUG
        DEBUG_TAG(1, m_debugName, "Event STOP");
#endif
        // handle state change
        emit fsmUpExited(QPrivateSignal());
        fsmDown();
        emit fsmDownEntered(QPrivateSignal());
        // execute actions
        stopHeartbeatTimer();
        stopSocket();
     }
}

/** start trigger function */
void RpcClient::start()
{
    if (m_state == State::Down) {
        emit fsmDownStart(QPrivateSignal());
    }
}

/** stop trigger function */
void RpcClient::stop()
{
    if (m_state == State::Trying) {
        emit fsmTryingStop(QPrivateSignal());
    }
    else if (m_state == State::Up) {
        emit fsmUpStop(QPrivateSignal());
    }
}

} } // namespace machinetalk::common
