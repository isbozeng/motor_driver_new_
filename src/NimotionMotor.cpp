#include "NimotionMotor.h"
#include <string.h>
#include <bitset>
#include<math.h>

static const std::string GREEN_BOLD = "\033[1;32m";
static const std::string RED_BOLD = "\033[1;31m";
static const std::string RESET_FORMAT = "\033[0m";

nimotionMotor::nimotionMotor(uint32_t id, bool inverse, uint32_t res)
    :CtrlMotorInterface(id, inverse)
    ,resolution(res)
{

}

nimotionMotor::~nimotionMotor()
{

}

int32_t nimotionMotor::init()
{
    can_bus_ = UsbCanBus::getCanBusInstance();
    if (can_bus_ != nullptr)
    {
        if (can_bus_->getOpenStatus() < 0)
        {
            return ERROR_CAN_DEVICE_FAIL;
        }
        memset(&statusword, 0, sizeof(statusword));
        memset(&controlword, 0, sizeof(controlword));
        can_bus_ = UsbCanBus::getCanBusInstance();
        can_bus_->CanReceiveRegister(0x180 | nodeID_, &nimotionMotor::recMsgCallback, this);
        can_bus_->CanReceiveRegister(0x280 | nodeID_, &nimotionMotor::recMsgCallback, this);
        can_bus_->CanReceiveRegister(0x380 | nodeID_, &nimotionMotor::recMsgCallback, this);
        can_bus_->CanReceiveRegister(0x480 | nodeID_, &nimotionMotor::recMsgCallback, this);
        can_bus_->CanReceiveRegister(0x580 | nodeID_, &nimotionMotor::recMsgCallback, this);



        last_time = std::chrono::steady_clock::now();
        now_time = last_time;
        run();//start  node;

    }
    return 0;
}

void nimotionMotor::run()
{
    CanBase::CanTxMsg msg;
    msg.ExtId = 0x00; // not used at all
    msg.IDE = CanBase::CAN_ID_STD;
    msg.RTR = CanBase::CAN_RTR_DATA;       // data frame
    // std::lock_guard<std::mutex> lock(mtx); // 锁定互斥锁
    if(mtx.try_lock())
    {
        // void *dat = &statusword;
        // std::cout << RED_BOLD << "statusword:0b " << std::bitset<16>(*((uint16_t *)dat)).to_string()
        //             << " id:" << (int)nodeID_
        //             << " error 0x:" << static_cast<int32_t>(error_code)
        //             << " mode:" << static_cast<int32_t>(nimotion_mode)
        //             << " cur_pos:" << cur_pos * 360.0 / 10000.0 / reduction << " pos_cmd:" << pos_cmd * 360.0 / 10000.0 / reduction
        //             << " cur_vel:" << cur_vel * 6.0 / reduction
        //             << " vel_cmd:" << vel_cmd * 60.0 * 6 / (10000.0 * reduction)
        //             << " in_state:" << (int)nimotion_state
        //             << " out_state:" << (int)state<< RESET_FORMAT << std::endl;    
        switchState();
        #ifdef DEBUG
        void *data = &statusword;
        std::cout << GREEN_BOLD << "statusword:0b " << std::bitset<16>(*((uint16_t *)data)).to_string()
                    << " id:" << (int)nodeID_
                    << " error 0x:" << static_cast<int32_t>(error_code)
                    << " mode:" << static_cast<int32_t>(nimotion_mode)
                    << " cur_pos:" << cur_pos * 360.0 / resolution / reduction << " pos_cmd:" << pos_cmd * 360.0 / resolution / reduction
                    << " cur_vel:" << cur_vel * 6.0 / reduction
                    << " vel_cmd:" << vel_cmd * 60.0 * 6 / (resolution * reduction)
                    << " in_state:" << (int)nimotion_state
                    << RESET_FORMAT << std::endl;//
        #endif

        //     std::cout << GREEN_BOLD << "nimotion_state:" <<(int)nimotion_state <<
        // " motor.state:"<< (int)state << RESET_FORMAT << std::endl;
        switch (nimotion_state)
        {
        case START_NODE:
        {
            if (!isOnline)
            {
                msg.StdId = 0x00;
                msg.DLC = 2;
                msg.Data[0] = 0x01;
                msg.Data[1] = 0;         // id;
                can_bus_->Transmit(msg); //   start node
                // nimotion_state = READY;
                std::cout << GREEN_BOLD << "NODE_ID:" << (uint32_t)nodeID_ << " START_NODE" << RESET_FORMAT << std::endl;                
            }
        }
        break;
        case READY:
        {
            if (statusword.ready != 1)
            {
                msg.StdId = 0x200 | nodeID_;
                msg.DLC = 3;
                controlword.enableOperation = 0;
                controlword.quickStop = 1;
                controlword.enableVol = 1;
                controlword.switchOn = 0;
                controlword.operationMode = 0;
                *((controlword_t *)&msg.Data) = controlword;
                can_bus_->Transmit(msg);
                std::cout << GREEN_BOLD << "NODE_ID:" << (uint32_t)nodeID_ << " READY"
                    << " ev:" << (int32_t)statusword.enableVol << " qs:" << (int32_t)statusword.quickStop << RESET_FORMAT << std::endl;
                // nimotion_state = SWITCH_ON;
            }
        }
        break;
        case SWITCH_ON:
        {
            if (statusword.switchOn != 1 || isEnable)
            {
                msg.StdId = 0x200 | nodeID_;
                msg.DLC = 3;
                controlword.enableOperation = 0;
                controlword.quickStop = 1;
                controlword.enableVol = 1;
                controlword.switchOn = 1;
                controlword.operationMode = 0;
                *((controlword_t *)&msg.Data) = controlword;
                msg.Data[2] = modeCmd;
                can_bus_->Transmit(msg);
                std::cout << GREEN_BOLD << "NODE_ID:" << (uint32_t)nodeID_ << " SWITCH_ON"
                        << " so:" << (int32_t)statusword.switchOn << RESET_FORMAT << std::endl;
            }
        }
        break;
        case CLEAR_ERROR:
        {
            if (error_code != 0)
            {
                CanBase::CanTxMsg msg;
                controlword_t ctrl;
                memset(&ctrl, 0, sizeof(controlword_t));
                msg.ExtId = 0x00; // not used at all
                msg.IDE = CanBase::CAN_ID_STD;
                msg.RTR = CanBase::CAN_RTR_DATA; // data frame
                msg.StdId = 0x200 | nodeID_;
                msg.DLC = 3;
                *((controlword_t *)&msg.Data) = ctrl;
                msg.Data[2] = modeCmd;
                can_bus_->Transmit(msg);
                if (statusword.ready == 0) // 控制字00生效
                {
                    ctrl.operationMode = 1;
                    can_bus_->Transmit(msg);
                }
            }
            else
            {
                resetCmd = false;
            }
            std::cout << GREEN_BOLD << "NODE_ID:" << (uint32_t)nodeID_ << " CLEAR_ERROR"
                    << " error_code:" << getError() << RESET_FORMAT << std::endl;
        }
        break;
        case HOMING:
        {
            if (clear_ack == NULL_CLEAR)
            {
                can_bus_->SDO_Write(nodeID_, 0x2031, 0x01, 1, 2);
            }
            else if (clear_ack == HIGHT_CLEAR)
            {
                if (cur_pos != 0)
                {
                    can_bus_->SDO_Write(nodeID_, 0x2031, 0x01, 0, 2);
                }
                else
                {
                    clear_ack = NULL_CLEAR;
                    homeCmd = false;
                }
            }
            std::cout << GREEN_BOLD << "NODE_ID:" << (uint32_t)nodeID_ << " HOMING"
            << " clear_ack:" << (int32_t)clear_ack
            << " cur_pos:"<< cur_pos
            <<RESET_FORMAT << std::endl;            
            // std::cout << GREEN_BOLD << "NODE_ID:" << (uint32_t)nodeID_ << " HOMING"
            //         << " clear_ack:" << (int32_t)clear_ack << RESET_FORMAT << std::endl;
        }
        break;
        case SET_MODE:
        {
            if(nimotion_mode != modeCmd)
            {
                msg.StdId = 0x200 | nodeID_;
                msg.DLC = 3;
                controlword.enableOperation = 0;
                controlword.quickStop = 1;
                controlword.enableVol = 1;
                controlword.switchOn = 1;
                *((controlword_t *)&msg.Data) = controlword;
                msg.Data[2] = modeCmd;
                can_bus_->Transmit(msg);
                std::cout << GREEN_BOLD << "NODE_ID:" << (uint32_t)nodeID_ << " SET_MODE"
                        << " mode:" << (int32_t)nimotion_mode << RESET_FORMAT << std::endl;
            }

        }
        break;
        case SET_ENABLE:
        {
            msg.StdId = 0x200 | nodeID_;
            msg.DLC = 3;

            *((controlword_t *)&msg.Data) = controlword;
            // if (cur_vel != 0) // 断使能
            // {
            //     controlword.enableOperation = 0;
            //     controlword.quickStop = 1;
            //     controlword.enableVol = 1;
            //     controlword.switchOn = 1;
            //     controlword.operationMode = 0;
            //     can_bus_->Transmit(msg);
            // }
            // else 
            if (statusword.enableOperation != 1) // 使能不再使能
            {
                controlword.enableOperation = 1;
                controlword.quickStop = 1;
                controlword.enableVol = 1;
                controlword.switchOn = 1;
                msg.Data[2] = modeCmd;
                if (modeCmd == IP_MODE)
                {
                    
                    controlword.operationMode = 0;
                }
                else
                {
                    controlword.operationMode = 2;
                }
                can_bus_->Transmit(msg);         
            }
        }
        break;
        case SET_CMD:
        {
            msg.StdId = 0x300 | nodeID_;
            msg.DLC = 8;
            *((int32_t *)&msg.Data) = pos_cmd;
            *((int32_t *)&msg.Data[4]) = vel_cmd;
            can_bus_->Transmit(msg);
            can_bus_->Transmit(msg);
            can_bus_->Transmit(msg);
            // std::cout << GREEN_BOLD << "NODE_ID:" <<(uint32_t)nodeID_<<" SET_CMD"<<
            // " pos_cmd:"<<(int32_t)pos_cmd<<" vel_cmd:"<<(int32_t)vel_cmd <<
            // " sdo_ack:"<<(int32_t)sdo_ack<<RESET_FORMAT << std::endl;

            nimotion_state = MOTION_ENABLE;
            isNewCmd = true;

            // if(sdo_ack == NULL_ACK)
            // {
            //     can_bus_->SDO_Read(nodeID_, 0x607a0010);//read pos cmd
            // }
            // else if (sdo_ack == POS_ACK)
            // {
            //     can_bus_->SDO_Read(nodeID_, 0x60810010);//read speed cmd
            // }
            // else if (sdo_ack == VEL_ACK)
            // {
            //     sdo_ack = NULL_ACK;
            //     nimotion_state = MOTION_ENABLE;
            // }
        }
        break;
        case MOTION_ENABLE:
        {
            msg.StdId = 0x200 | nodeID_;
            msg.DLC = 3;
            controlword.enableOperation = 1;
            controlword.quickStop = 1;
            controlword.enableVol = 1;
            controlword.switchOn = 1;
            controlword.operationMode = 2;
            *((controlword_t *)&msg.Data) = controlword;
            msg.Data[2] = POS_MODE;
            can_bus_->Transmit(msg); // 当前有任务，使能情况下，还需再使能
            if (statusword.enableOperation == 1 && statusword.operationMode == 0)
            {
                nimotion_state = MOTION;
            }
            // std::cout << GREEN_BOLD << "NODE_ID:" <<(uint32_t)nodeID_<<" MOTION_ENABLE"<<
            // " switchOn:"<<(int32_t)statusword.switchOn<<" operationMode:"<<
            // (int32_t)statusword.operationMode <<RESET_FORMAT << std::endl;
        }
        break;
        case MOTION:
        {
            if (isNewCmd || (cur_vel == 0 && cur_pos != pos_cmd) /*abs(cur_pos - pos_cmd) > det_pos*/)
            {
                msg.StdId = 0x200 | nodeID_;
                msg.DLC = 3;
                controlword.switchOn = 1;
                controlword.enableVol = 1;
                controlword.quickStop = 1;
                controlword.enableOperation = 1;
                controlword.operationMode = 3;
                *((controlword_t *)&msg.Data) = controlword;
                msg.Data[2] = POS_MODE;
                can_bus_->Transmit(msg);
                isNewCmd = false;
            }
            else if (statusword.targetReached == 1 || cur_vel == 0)
            {
                /* code */
                nimotion_state = DONE;
            }
            // std::cout << GREEN_BOLD << "NODE_ID:" <<(uint32_t)nodeID_<<" MOTION"<<
            // " cur_vel:"<<(int32_t)cur_vel<<" cur_pos:"<<(int32_t)cur_pos<<" targetReached:"<<
            // (int32_t)statusword.targetReached <<RESET_FORMAT << std::endl;
        }
        break;
        case DONE:
        {
            nimotion_state = SET_ENABLE;
        }
        }

        mtx.unlock();
    }    
}

void nimotionMotor::recMsgCallback(CanBase::CanRxMsg msg)
{
    std::lock_guard<std::mutex> lock(mtx); // 锁定互斥锁
    switch (msg.StdId - nodeID_)
    {
    case (0x180):
    {
        statusword = *((statusword_t *)&msg.Data);
        error_code = *((uint32_t *)&msg.Data[2]);
        nimotion_mode = static_cast<NI_MOTION_MODE_t>(msg.Data[4]);
        last_time = std::chrono::steady_clock::now();
        isOnline = true;
        // void *data = &statusword;
        // std::cout << GREEN_BOLD << "statusword:0b " << std::bitset<16>(*((uint16_t *)data)).to_string()
        //           << " id:" << (int)nodeID_
        //           << " error 0x:" << static_cast<int32_t>(error_code)
        //           << " mode:" << static_cast<int32_t>(nimotion_mode)
        //           << " cur_pos:" << cur_pos * 360.0 / 10000.0 / reduction << " pos_cmd:" << pos_cmd * 360.0 / 10000.0 / reduction
        //           << " cur_vel:" << cur_vel * 6.0 / reduction
        //           << " vel_cmd:" << vel_cmd * 60.0 * 6 / (10000.0 * reduction)
        //           << " in_state:" << (int)nimotion_state
        //           << " out_state:" << (int)state << std::endl;
    }
    break;
    case (0x280):
    {
        cur_vel = *((int32_t *)&msg.Data);
        // std::cout << GREEN_BOLD << "cur_vel:" <<cur_vel<<RESET_FORMAT << std::endl;
    }
    break;
    case (0x380):
    {
        current = *((int16_t *)&msg.Data);
        sixForce = *((int16_t *)&msg.Data[2]);
        // std::cout << GREEN_BOLD << "current:" <<(int32_t)current<<" sixForce:"<<(int32_t)sixForce<<RESET_FORMAT << std::endl;
    }
    break;
    case (0x480):
    {
        cur_pos = *((int32_t *)&msg.Data);
        isRecPos = true;
        // if (!isOnline)
        // {
        //     pos_cmd = cur_pos;//掉线恢复后不能运动
        // }
        // isOnline = true;
        // angle = cur_pos * 360.0 / 10000.0 / reduction;
        // std::cout << GREEN_BOLD << "cur_pos:" <<cur_pos<<RESET_FORMAT << std::endl;
    }
    break;
    case (0x580):
    {
        if (msg.Data[0] == 0x43)
        {
            switch (msg.Data[1])
            {
            case 0x7a: // pos
            {
                if (!(sdo_ack == POS_ACK))
                {
                    if (pos_cmd == *((int32_t *)&msg.Data[4]))
                    {
                        sdo_ack = POS_ACK;
                    }
                }
                // std::cout << GREEN_BOLD << "sdo reply:"<<std::hex << (int32_t)msg.Data[0] <<
                // " index:"<<std::hex<<(int32_t)msg.Data[2]<<std::hex<<(int32_t)msg.Data[1] <<
                // " pos_cmd:" <<(int32_t)pos_cmd<<" vel_cmd:"<<(int32_t)vel_cmd<<
                // " sdo_ack:"<<(int32_t)sdo_ack<<" read_pos_cmd:"<<*((int32_t*)&msg.Data[4]) <<
                // " 0x"<<std::hex<<(int32_t)msg.Data[4]<<
                // " "<<(int32_t)msg.Data[5]<<" "<<(int32_t)msg.Data[6] <<
                // " "<<(int32_t)msg.Data[7]<<RESET_FORMAT << std::endl;
            }
            break;
            case 0x81: // vel
            {
                if (!(sdo_ack == VEL_ACK))
                {
                    if (vel_cmd == *((int32_t *)&msg.Data[4]) && sdo_ack == POS_ACK)
                    {
                        sdo_ack = VEL_ACK;
                    }
                }
                // std::cout << GREEN_BOLD << "sdo reply:"<<std::hex << (int32_t)msg.Data[0] <<
                // " index:"<<std::hex<<(int32_t)msg.Data[2]<<std::hex<<(int32_t)msg.Data[1] <<
                // " pos_cmd:" <<(int32_t)pos_cmd<<" vel_cmd:"<<(int32_t)vel_cmd<<
                // " sdo_ack:"<<(int32_t)sdo_ack<<" read_vel_cmd:"
                // <<*((int32_t*)&msg.Data[4])<<
                // " 0x"<<std::hex<<(int32_t)msg.Data[4]<<
                // " "<<(int32_t)msg.Data[5]<<" "<<(int32_t)msg.Data[6] <<
                // " "<<(int32_t)msg.Data[7]<<RESET_FORMAT << std::endl;
            }
            break;
            }
        }
        else if (msg.Data[0] == 0x60)
        {
            if (msg.Data[1] == 0x31) // clear encoder
            {
                if (clear_ack == NULL_CLEAR)
                {
                    clear_ack = HIGHT_CLEAR;
                    // can_bus_->SDO_Write(1, 0x2031, 0x01, 0, 2);
                }
                // else if(clear_ack = HIGHT_CLEAR)
                // {
                //     clear_ack = NULL_CLEAR;
                //     isHome = false;
                // }
                // std::cout<<"clear_ack:"<<clear_ack<<std::endl;
            }
        }
        // std::cout << GREEN_BOLD << "sdo reply:"<<std::hex << (int32_t)msg.Data[0] <<
        // " index:"<<std::hex<<(int32_t)msg.Data[2]<<std::hex<<(int32_t)msg.Data[1] <<
        // " pos_cmd:" <<(int32_t)pos_cmd<<" vel_cmd:"<<(int32_t)vel_cmd<<
        // " sdo_ack:"<<(int32_t)sdo_ack<<" read_pos_cmd:"<<*((int32_t*)&msg.Data[4])<<
        // " read_vel_cmd:"<<*((int32_t*)&msg.Data[4])
        // RESET_FORMAT << std::endl;
    }
    break;
    }

    // 超时检测
    now_time = std::chrono::steady_clock::now();
    auto elapsedTime = now_time - last_time;
    int32_t delt = std::chrono::duration_cast<std::chrono::seconds>(elapsedTime).count();
    // std::cout<<"delt:"<<delt<<std::endl;
    if (delt > timeout)
    {
        isOnline = false;
        isRecPos = false;
        std::cout << RED_BOLD << "nodeID_:" << (int)nodeID_ << " is offline!" << RESET_FORMAT << std::endl;
    }
}

void nimotionMotor::switchState()
{
    isEnable = (bool)statusword.enableOperation;
                    
    if (!isOnline) // 离线状态
    {
        nimotion_state = START_NODE;
    }
    else if (error_code != 0) // 错误状态
    {
        if (resetCmd) // 是否恢复
        {
            nimotion_state = CLEAR_ERROR;
        }
        else
        {
            // nimotion_state = SWITCH_ON; // disable motor
        }
        std::cout << RED_BOLD << "nodeID_:" << (int)nodeID_ << " error:" << std::hex << (uint32_t)error_code <<std::dec << RESET_FORMAT << std::endl;
    }
    else if (statusword.ready != 1) // 电机ready
    {
        nimotion_state = READY;
    }
    else if (statusword.switchOn != 1) // 电机switch on
    {
        nimotion_state = SWITCH_ON;
    }
    else if (statusword.enableOperation != 1) // 电机使能
    {   
        if (homeCmd) // 是否回零，电机不使能才可回零
        {
            nimotion_state = HOMING;
        }
        else
        {
            if (enableCmd)
            {
                if (nimotion_mode == IP_MODE)
                {
                    pos_cmd = cur_pos; // 使能后不能运动，指令清除
                    CanBase::CanTxMsg msg;
                    msg.ExtId   = 0x00;     // not used at all
                    msg.IDE     = CanBase::CAN_ID_STD;
                    msg.RTR     = CanBase::CAN_RTR_DATA;        // data frame
                    msg.StdId   = 0x400 | nodeID_;
                    msg.DLC     = 4; 
                    *((int32_t *)&msg.Data) = cur_pos;
                    can_bus_->Transmit(msg); 
                    can_bus_->Transmit(msg);  
                }
                nimotion_state = SET_ENABLE;
            }
            else
            {
                nimotion_state = SWITCH_ON;
            }
        }
    }
    else if (homeCmd) // 使能状态检查是否需要回零
    {
        nimotion_state = SWITCH_ON;
    }
    else if (!enableCmd) // 电机去使能
    {
        nimotion_state = SWITCH_ON;
    }
    else if (modeCmd != nimotion_mode)// 电机模式是否是IP模式
    {
         nimotion_state = SET_MODE; 
    }
    else if (nimotion_state < SET_ENABLE) // nimotion_state状态更新
    {
        nimotion_state = SET_ENABLE;
    }
 
}


int32_t nimotionMotor::setMotorPosition(double pos)
{
    if (error_code != 0)
    {
        return getError();
    }
    if (isEnable && nimotion_mode == IP_MODE)    
    {
        // int reverse = 1;
        // if(inverse_direction_ && pos < 0.0)
        // {
        //     reverse = -1;
        // }
        pos = pos * 180 / M_PI;
        pos = inverse_direction_ ? -pos : pos;
        // if(mtx.try_lock())
        // {
        //     angle = reverse * cur_pos * 360.0 / 10000.0;
            
        //     mtx.unlock();
        // }
        // double cur_angle = angle;
        // if (fabs(angle - pos) < 6)
        // {
            CanBase::CanTxMsg msg;
            msg.ExtId   = 0x00;     // not used at all
            msg.IDE     = CanBase::CAN_ID_STD;
            msg.RTR     = CanBase::CAN_RTR_DATA;        // data frame
            msg.StdId   = 0x400 | nodeID_;
            msg.DLC     = 4; 
            int32_t temp_pos = pos * resolution / 360.0;
            *((int32_t *)&msg.Data) = temp_pos;

            // can_bus_->SDO_Write(nodeID, 0x60c1, 0x01, temp_pos, 4);
            
            can_bus_->Transmit(msg);
        // }
        // else
        // {
        //     std::cout << RED_BOLD << "nodeid:" << (int)nodeID << " setAngle to max:"<< _angle<<"cur_angle:"<<cur_angle << RESET_FORMAT << std::endl;
        // }
        return 0;
    }        
    return -1;
}

int32_t nimotionMotor::getMotorPosition(double& pos)
{
    if (error_code != 0 || !isRecPos)
    {
        return getError();
    }
    pos = cur_pos * 360.0 / resolution;
    pos = pos * M_PI / 180;
    if(inverse_direction_)
    {
        pos = -pos;
    }
    return 0;
}

int32_t nimotionMotor::setEnableStatus(bool enable)
{
    if (error_code != 0)
    {
        return getError();
    } 
    enableCmd = enable;
    return 0;
}

int32_t nimotionMotor::getEnableStatus(bool& enable)
{
    run();
    if (error_code != 0)
    {
        return getError();
    }
    
    if (!isOnline)
        return ERROR_DEVICE_OFFLINE;
    enable = isEnable;
    return 0;
}

int32_t nimotionMotor::setHome(uint32_t timeout)
{
    if (error_code != 0)
    {
        return getError();
    }
    auto setHomeLastTime = std::chrono::steady_clock::now();
    auto setHomeNowTime = std::chrono::steady_clock::now();
    auto elapsedTime = setHomeNowTime - setHomeLastTime;
    homeCmd = true;
    uint32_t delt = 0;
    while (delt < timeout && homeCmd)
    {
        run();
        usleep(10000);
        setHomeNowTime = std::chrono::steady_clock::now();
        elapsedTime = setHomeNowTime - setHomeLastTime;
        
        delt = std::chrono::duration_cast<std::chrono::milliseconds>(elapsedTime).count(); 
    }
    if (delt >= timeout)
    {
        return ERROR_SET_HOME_TIMEOUT;
    }
    return CMD_SUCCESS;
}

int32_t nimotionMotor::getTorque(double& torque)
{
    if (error_code != 0 || !isOnline)
    {
        return getError();
    }
    torque = sixForce*0.001;
    return 0;
}

int32_t nimotionMotor::getVelocity(double& velocity)
{
    if (error_code != 0 || !isOnline)
    {
        return getError();
    }
    velocity = cur_vel * 6.0;
    return 0;    
}

int32_t nimotionMotor::getError()
{
   int32_t ret = 0;
   if (error_code)
   {
        std::cout << RED_BOLD << "nodeID_:" << (int)nodeID_ << " error:" << std::hex << (uint32_t)error_code <<std::dec << RESET_FORMAT << std::endl;
   }
   if (!isRecPos)
    return ERROR_UNRECIVER_POS;
   if (!isOnline)
    return ERROR_DEVICE_OFFLINE;
   switch(error_code)
   {
    case 0x2300: ret = ERROR_CURRENT_OVERLOAD; break;
   }
   return ret;
}