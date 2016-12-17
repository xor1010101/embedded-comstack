/**
 * @file      CanSocket.h
 * @author    dtuchscherer <daniel.tuchscherer@hs-heilbronn.de>
 * @brief     CAN interface to send and receive CAN frames over SocketCAN.
 * @details   This is a CAN module to send and receive data over CAN bus.
 *            It is designed for Linux systems. The CAN communication
 *            is done via SocketCAN.
 * @version   2.0
 * @copyright Copyright (c) 2015, dtuchscherer.
 *            All rights reserved.
 *
 *            Redistributions and use in source and binary forms, with
 *            or without modifications, are permitted provided that the
 *            following conditions are met: Redistributions of source code must
 *            retain the above copyright notice, this list of conditions and the
 *            following disclaimer.
 *
 *            Redistributions in binary form must reproduce the above copyright
 *            notice, this list of conditions and the following disclaimer in
 *            the documentation and/or other materials provided with the
 *            distribution.
 *
 *            Neither the name of the Heilbronn University nor the name of its
 *            contributors may be used to endorse or promote products derived
 *            from this software without specific prior written permission.
 *
 *            THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS “AS IS”
 *            AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *            TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *            PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS
 *            OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *            SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *            LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *            USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *            AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *            LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *            ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *            POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef CAN_H_
#define CAN_H_
#ifndef _WIN32
/*******************************************************************************
 * MODULES USED
 ******************************************************************************/

#include "ComStack_Types.h" // @req AUTOSAR CAN388
#include "Socket.h"         // uses sockets under Linux

#include <array>           // rx, tx
#include <cstring>         // strcopy for interface name
#include <linux/can.h>     // sockaddr structure, protocols and can_filter
#include <linux/can/raw.h> // filtering
#include <net/if.h>        // interface name
#include <sys/ioctl.h>     // blocking / non-blocking
#include <unistd.h>        // write and read for CAN interface

/*******************************************************************************
 * DEFINITIONS AND MACROS
 ******************************************************************************/

/*******************************************************************************
 * TYPEDEFS, ENUMERATIONS, CLASSES
 ******************************************************************************/

struct CAN_STD
{
    // the standard CAN frame contains a total maximum of 8 bytes of user data.
    static constexpr auto DATA_LEN{8U};
};

struct CAN_FD
{
    // the CAN FD frame contains a total maximum of 64 bytes of user data.
    static constexpr auto DATA_LEN{64U};
};

//! Forward type. You may use these types to declare data packets to send.
using CanDataType = std::array< AR::uint8, CAN_STD::DATA_LEN >;
using CanStdData = CanDataType;
using CanFDData = std::array< AR::uint8, CAN_FD::DATA_LEN >;
using CanIDType = canid_t;

/**
 * @brief CanSocket is used for sending and receiving standard CAN frames and
 * CAN FD frames.
 */
class CanSocket : public Socket< CanSocket >
{
  public:
    /**
     * @brief Default constructor initializing the socket base class,
     * initializing the CAN.
     * @param[in] ifrname interface name, e.g. "can0", "vcan0"
     */
    template < std::size_t N >
    CanSocket(const char (&interface_str)[N]) noexcept
        : Socket{SocketType::CAN}, m_can_init{FALSE}
    {
        // before we set up the CAN interface, create a socket to send and
        // receive data through.
        AR::boolean sock_created = is_socket_initialized();

        if (sock_created == TRUE)
        {
            // check if the interface is registered
            AR::boolean interface_exists = check_interface(interface_str);

            // socket creation successful and interface exists
            if (interface_exists == TRUE)
            {
                AR::boolean bind_success = bind_if_socket();

                if (bind_success == TRUE)
                {
                    // we turn on CAN FD mode automatically as a standard
                    // setting.
                    // This makes it possible to send both standard frames and
                    // CAN FD frames.
                    const bool canfd = enable_canfd();
                    m_can_init = canfd;
                }
                else
                {
                    std::cout << "Binding the interface to the created socket "
                                 "failed.\n";
                    m_can_init = FALSE;
                }
            }
            else
            {
                // the interface specified is not available.
                std::cout << "CAN interface " << interface_str
                          << " specified is not available.\n";
                m_can_init = FALSE;
            }
        }
        else
        {
            std::cout << "Socket creation failed.\n";
            m_can_init = FALSE;
        }
    }

    /**
     * @brief Transmits a message on CAN bus.
     * @param[in] can_id CAN identifier to transmit the message.
     * @param[in] data This is an array of bytes that contains
     * data and is being transmitted on CAN bus with the given CAN ID and DLC.
     * @param[in] len Length in bytes to send. This is then written to the DLC
     * field.
     * One standard CAN frame may contain 8 bytes as a maximum.
     * @return the length transmitted. If send was successfull this must be
     * the number of bytes equally to CAN_MTU = 16 for standard frames and
     * CANFD_MTU = 72 for CAN FD frames.
     * @remarks If you want to send more than 8 bytes / 64 bytes as a complete
     * message, you need a transport layer such as CanTp / ISO-TP.
     */
    template < typename CANFrame >
    AR::sint8 send(const CanIDType can_id, const CANFrame& data,
                   const AR::uint8 len) noexcept
    {
        // initialize with zero value. uninitialized stack variables are a
        // common
        // error source.
        AR::sint8 data_sent{0};
        // must be the correct array size. minimizes static analysis efforts by
        // checking this at compile-time
        static_assert(std::is_same< CanStdData, CANFrame >::value ||
                          std::is_same< CanFDData, CANFrame >::value,
                      "Must be a standard CAN frame or CAN FD frame.");

        // select the can frame type: may be a standard CAN frame or CAN FD
        // frame
        typedef typename std::conditional<
            std::is_same< CanStdData, CANFrame >::value, struct can_frame,
            struct canfd_frame >::type SelectedFrame;

        // First check if the file descriptor for the socket was initialized
        // and the interface is up and running.
        if ((is_can_initialized() == TRUE) && (len > 0U))
        {
            // Structure that is given to the POSIX write function.
            // We have to define CAN-ID, length (DLC) and copy the data to send.
            struct canfd_frame frame;
            // clear all to make sure we don't send rubbish.
            std::memset(&frame, 0, sizeof(SelectedFrame));
            // set the CAN ID of this message to send
            frame.can_id = can_id;
            // check if length is possible for one CAN frame...
            // one CAN frame may only contain 8 data bytes.
            // limit the length to max DLC of CAN
            frame.len = std::min(static_cast< decltype(data.size()) >(len),
                                 data.size());

            // copy data into the data field of the frame struct.
            for (AR::uint8 i = 0U; i < len; ++i)
            {
                // copy byte by byte...
                // this could also be done by memcpy.
                frame.data[i] = data[i];
            }

            const auto socket = get_socket_handle();
            // We need to transmit the size of struct can_frame with a POSIX
            // write.
            const auto send_res = write(socket, &frame, sizeof(SelectedFrame));
            data_sent = static_cast< AR::sint8 >(send_res);

            // Check if the desired length was transmitted over the socket.
            // A maximum of 8 bytes can be transmitted within a CAN message
            // without using IsoTp.
            if (data_sent <= 0)
            {
                std::cout << "Send failed with error number: " << errno << "\n";
                // store the transport layer error number, other layers may
                // access to do an advanced and application specific error
                // handling.
                m_last_error = errno;
                data_sent = -1;
            }
        }
        else
        {
            // the CAN interface is not initialized correctly.
            data_sent = -1;
        }

        return data_sent;
    }

    /**
     * @brief Receives a CAN message from the socket and
     * writes the data into an array (blocking read).
     * @param[out] can_id CAN identifier of the message received.
     * @param[out] data_ref Array to store the received packet data to.
     * @return Greater than zero if data was received.
     * This returns -1 if there was an error or timeout.
     */
    AR::sint8 receive(CanIDType& can_id, CanFDData& data_ref) noexcept;

    /**
     * @brief Receives a CAN message from the socket and writes the data into
     * an array (non-blocking read / timeout / polling possible).
     * @param[out] can_id   CAN identifier of the message received.
     * @param[out] data_ref Array to store the received packet data to.
     * @param[in] timeout timeout of the read function in micro seconds
     * @return Greater than zero if data was received.
     * If there is a timeout it returns zero.
     * If there was an error, -1 is transmitted.
     */
    AR::sint8 receive(CanIDType& can_id, CanDataType& data_ref,
                      const AR::uint16 timeout_us) noexcept;

    /**
     * @brief Create a CAN socket / file descriptor to send and receive.
     * @return true if the socket is opened or false if there was an error.
     */
    AR::boolean create() noexcept;

    /**
     * @brief Checks if the CAN is initialized properly.
     */
    AR::boolean is_can_initialized() const noexcept;

    /**
     * @brief Switch to CAN FD mode. Configure the interface to send and receive
     * CAN FD frames.
     * @return true if enabling CAN FD was possible, false if not.
     */
    AR::boolean enable_canfd() noexcept;

  protected:
  private:
    /**
     * @brief Check if the interface exists and is known to the OS.
     * @param[in] interface_str the interface name as string.
     * @return true if the interface is known. If the system
     * does not know the given interface it will return false.
     */
    template < std::size_t N >
    AR::boolean check_interface(const char (&interface_str)[N]) noexcept
    {
        AR::boolean exists{FALSE};

        // copying the interface string into the structure.
        std::strncpy(m_ifr.ifr_name, interface_str, IFNAMSIZ - 1U);

        // make sure that the string is terminated.
        m_ifr.ifr_name[IFNAMSIZ - 1U] = '\0';

        // map the interface string to a given interface of the system.
        m_ifr.ifr_ifindex = if_nametoindex(m_ifr.ifr_name);

        // check if the CAN interface is known to the system
        if (m_ifr.ifr_ifindex != 0)
        {
            // the CAN interface is known to the system
            exists = TRUE;
        }
        else
        {
            // ... error finding the given interface. The interface is not known
            // to
            // the system.
            exists = FALSE;
        }

        return exists;
    }

    /**
     * @brief If the interface exists this will bind the interface to the
     * socket.
     * @return true if binding the interface to the socket was succesful -
     * otherwise false
     */
    AR::boolean bind_if_socket() noexcept;

    //! Holds the index of the interface in a struct if
    //! the interface exists. Works as a handle for configuration.
    struct ifreq m_ifr;

    //! Holds the address family CAN and
    //! the interface index to bind the socket to.
    struct sockaddr_can m_sockaddr;

    //! Whether the socket creation, binding and interface is ok, configured or
    //! not.
    AR::boolean m_can_init;

    // determine the MTU with the help of the struct on compile-time
    static constexpr auto m_can_mtu = sizeof(struct canfd_frame);
};

/*******************************************************************************
 * EXPORTED VARIABLES
 ******************************************************************************/

/*******************************************************************************
 * EXPORTED FUNCTIONS
 ******************************************************************************/

#endif // WIN32 detection
#endif // CAN_H
