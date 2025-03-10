/*
 * Copyright 2022 iLogtail Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <atomic>
#include <json/json.h>
#include <yaml-cpp/yaml.h>
#include "common/LogtailCommonFlags.h"
#include "common/MemoryBarrier.h"
#include "common/LogstoreFeedbackQueue.h"
#include "config/Config.h"
#include "common/MemoryBarrier.h"
#include "common/Lock.h"
#include "common/Thread.h"
#include "event/Event.h"
#include "sdk/Common.h"

DECLARE_FLAG_BOOL(https_verify_peer);
DECLARE_FLAG_STRING(https_ca_cert);
DECLARE_FLAG_INT32(request_access_key_interval);
DECLARE_FLAG_STRING(profile_project_name);
DECLARE_FLAG_INT32(logtail_sys_conf_update_interval);
DECLARE_FLAG_STRING(fuse_customized_config_name);
DECLARE_FLAG_INT32(default_max_depth_from_root);
DECLARE_FLAG_BOOL(logtail_config_update_enable);

namespace logtail {

enum ParseConfResult { CONFIG_OK, CONFIG_NOT_EXIST, CONFIG_INVALID_FORMAT };
ParseConfResult ParseConfig(const std::string& configName, Json::Value& jsonRoot);
ParseConfResult ParseConfig(const std::string& configName, YAML::Node& yamlRoot);

void ReplaceEnvVarRefInConf(Json::Value& rawValue);
std::string replaceEnvVarRefInStr(const std::string& inStr);

class LogFileReader;
class EventDispatcher;
class EventHandler;
struct LogFilterRule;

template<class T>
class DoubleBuffer {
public:
    DoubleBuffer() : currentBuffer(0) {}

    T& getWriteBuffer() {
        return buffers[currentBuffer];
    }

    T& getReadBuffer() {
        return buffers[1 - currentBuffer];
    }

    void swap() {
        currentBuffer = 1 - currentBuffer;
    }
private:
    T buffers[2];
    int currentBuffer;
};

class ConfigManagerBase {
protected:
    int32_t mStartTime;

    Json::Value mConfigJson;
    Json::Value mLocalConfigJson;
    Json::Value mFileTagsJson;
    std::unordered_map<std::string, Json::Value> mLocalConfigDirMap;
    std::unordered_map<std::string, YAML::Node> mYamlConfigDirMap;

    std::unordered_map<std::string, int64_t> mServerYamlConfigVersionMap; // the key is config name
    std::unordered_map<std::string, int64_t> mYamlConfigMTimeMap; // the key is config name
    SpinLock mPluginStatsLock;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> mPluginStats;

    std::unordered_map<std::string, Config*> mNameConfigMap;
    EventHandler* mSharedHandler;
    // one modify handler corresponds to one "leaf" directory
    std::unordered_map<std::string, EventHandler*> mDirEventHandlerMap;
    ThreadPtr mUUIDthreadPtr;
    volatile bool mThreadIsRunning;
    volatile bool mRemoveConfigFlag;
    volatile CheckUpdateStat mUpdateStat;
    std::vector<EventHandler*> mHandlersToDelete;

    SpinLock mProfileLock;
    RegionType mRegionType;

    std::string mDefaultPubAliuid;
    std::string mDefaultPubAccessKeyId;
    std::string mDefaultPubAccessKey;

    SpinLock mDefaultPubAKLock;

    std::unordered_map<std::string, UserInfo*> mUserInfos;
    PTMutex mUserInfosLock;

    std::unordered_map<std::string, std::string> mMappingPaths;
    PTMutex mMappingPathsLock;
    bool mMappingPathsChanged;
    bool mHaveMappingPathConfig;

    volatile bool mEnvFlag;

    SpinLock mAliuidSetLock;
    std::set<std::string> mAliuidSet;

    SpinLock mProjectSetLock;
    std::set<std::string> mProjectSet;

    mutable SpinLock mRegionSetLock;
    std::set<std::string> mRegionSet;

    SpinLock mUserDefinedIdSetLock;
    std::set<std::string> mUserDefinedIdSet;
    std::string mUserDefinedId;

    std::string mDefaultProfileProjectName;
    std::string mDefaultProfileRegion;
    // Mapping from region name to related profile project name.
    std::unordered_map<std::string, std::string> mAllProfileProjectNames;

    int32_t mRapidUpdateConfigTryCount = 0;
    int32_t mLogtailSysConfUpdateTime;
    std::string mUUID;
    std::string mInstanceId;
    std::string mSessionId;
    int32_t mProcessStartTime;
    std::atomic_int mConfigUpdateTotal{0};
    std::atomic_int mConfigUpdateItemTotal{0};
    std::atomic_int mLastConfigUpdateTime{0};
    std::atomic_int mLastConfigGetTime{0};

    SpinLock mCacheFileConfigMapLock;
    // value : best config, multi config last alarmTime, and if alarmTime is 0, it means no multi config
    std::unordered_map<std::string, std::pair<Config*, int32_t>> mCacheFileConfigMap;

    SpinLock mCacheFileAllConfigMapLock;
    std::unordered_map<std::string, std::pair<std::vector<Config*>, int32_t>> mCacheFileAllConfigMap;

    PTMutex mDockerContainerPathCmdLock;
    std::vector<DockerContainerPathCmd*> mDockerContainerPathCmdVec;

    PTMutex mDockerContainerStoppedCmdLock;
    std::vector<DockerContainerPathCmd*> mDockerContainerStoppedCmdVec;

    /**
     * @brief mAllDockerContainerPathMap. when config update, we dump all config's std::vector<DockerContainerPath> to
     * mAllDockerContainerPathMap. And reset to config when LoadSingleUserConfig
     */
    std::unordered_map<std::string, std::shared_ptr<std::vector<DockerContainerPath>>> mAllDockerContainerPathMap;

    PTMutex mDockerMountPathsLock;
    DockerMountPaths mDockerMountPaths;

    bool mCollectionMarkFileExistFlag = false;
    mutable PTMutex mCollectionMarkLock;

    bool mHaveFuseConfigFlag = false;

    PTMutex mRegionAliuidMapLock;
    std::map<std::string, std::set<std::string>> mRegionAliuidMap;

    /**
     * @brief CreateCustomizedFuseConfig, call this after starting, insert it into config map
     * @return
     */
    virtual void CreateCustomizedFuseConfig() = 0;

    ConfigManagerBase();
    virtual ~ConfigManagerBase();

    bool GetUUIDThread();

    void SetUUID(std::string uuid) {
        mProfileLock.lock();
        mUUID = uuid;
        mProfileLock.unlock();
        return;
    }

    bool LoadGlobalConfig(const Json::Value& jsonRoot);
    bool DumpConfigToLocal(std::string fileName, const Json::Value& configJson);

    void SetDefaultProfileProjectName(const std::string& profileProjectName);
    void SetProfileProjectName(const std::string& region, const std::string& profileProject);

    /**
     * delete mHandlersToDelete and mReaderToDelete
     */
    void DeleteHandlers();

public:
    // @configExistFlag indicates if there are loaded configs before calling.
    virtual void InitUpdateConfig(bool configExistFlag);
    bool ParseTimeZoneOffsetSecond(const std::string& logTZ, int& logTZSecond);

    bool TryGetUUID();

    int32_t GetStartTime() { return mStartTime; }

    int32_t GetConfigUpdateTotalCount();
    int32_t GetConfigUpdateItemTotalCount();
    int32_t GetLastConfigUpdateTime();
    int32_t GetLastConfigGetTime();

    void RestLastConfigTime();

    /** Read configuration, detect any format errors.
     *
     * @param configFile path for the configuration file
     *
     * @return 0 on success; nonzero on error
     */
    virtual bool LoadConfig(const std::string& configFile) = 0;

    /**
     * @brief Load all name config in @jsonRoot to mNameConfigMap
     *
     * @param jsonRoot
     * @param localFlag
     * @return true always return true
     * @return false never return false
     */
    bool LoadJsonConfig(const Json::Value& jsonRoot, bool localFlag = false);
    void UpdatePluginStats(const Json::Value& config);
    std::string GeneratePluginStatString();
    void ClearPluginStats();

    bool LoadAllConfig();
    const std::unordered_map<std::string, Config*>& GetAllConfig() { return mNameConfigMap; }
    void RegisterWildcardPath(Config* config, const std::string& path, int32_t depth);
    bool RegisterHandlers(const std::string& basePath, Config* config);
    bool RegisterHandlers();
    bool RegisterHandlersRecursively(const std::string& dir, Config* config, bool checkTimeout);
    /**
     * @brief HasFuseConfig
     * @return true if global fuse flag is true and there is
     */
    bool HaveFuseConfig() { return mHaveFuseConfigFlag; }

    virtual bool UpdateAccessKey(const std::string& aliuid,
                                 std::string& accessKeyId,
                                 std::string& accessKey,
                                 int32_t& lastUpdateTime)
        = 0;

    int32_t GetUserAK(const std::string& aliuid, std::string& accessKeyId, std::string& accessKey);
    void SetUserAK(const std::string& aliuid, const std::string& accessKeyId, const std::string& accessKey);
    virtual void CleanUnusedUserAK() = 0;

    std::string GetDefaultPubAliuid();
    void SetDefaultPubAliuid(const std::string& aliuid);

    std::string GetDefaultPubAccessKeyId();
    void SetDefaultPubAccessKeyId(const std::string& accessKeyId);

    std::string GetDefaultPubAccessKey();
    void SetDefaultPubAccessKey(const std::string& accessKey);

    std::string GetProfileProjectName(const std::string& region, bool* existFlag = NULL);

    std::string GetUUID() {
        mProfileLock.lock();
        std::string uuid(mUUID);
        mProfileLock.unlock();
        return uuid;
    }

    std::string GetInstanceId() { return mInstanceId; }
    std::string GetSessionId() { return mSessionId; }

    void InsertAliuidSet(const std::string& aliuid);
    void SetAliuidSet(const std::vector<std::string>& aliuidList);
    void GetAliuidSet(Json::Value& aliuidArray);
    std::string GetAliuidSet();

    void SetUserDefinedIdSet(const std::vector<std::string>& userDefinedIdList);
    void GetUserDefinedIdSet(Json::Value& userDefinedIdArray);
    std::string GetUserDefinedIdSet();

    RegionType GetRegionType() { return mRegionType; }

    bool CheckRegion(const std::string& region) const;

    void GetAllProfileRegion(std::vector<std::string>& allRegion);
    std::string GetDefaultProfileProjectName();
    void SetDefaultProfileRegion(const std::string& profileRegion);
    std::string GetDefaultProfileRegion();

    /**
     * @brief Reload aliuid/user_defined_id from disk&env
     *
     */
    void ReloadLogtailSysConf();
    void CorrectionAliuidFile(const Json::Value& aliuidArray);
    void CorrectionAliuidFile();
    void CorrectionLogtailSysConfDir();

    void GetAllPluginConfig(std::vector<Config*>& configVec);
    void GetAllObserverConfig(std::vector<Config*>& configVec);

    // Get all configs that match condition.
    std::vector<Config*> GetMatchedConfigs(const std::function<bool(Config*)>& condition);

    /** find che config of DIR path while the name fits the file pattern
     *  @param path for the dir
     *  @name for the file name
     *
     *  Or if name is empty, find the config of parent of path
     *  @param path for the newly created dir
     *
     */
    Config* FindBestMatch(const std::string& path, const std::string& name = "");

    int32_t FindAllMatch(std::vector<Config*>& allConfig, const std::string& path, const std::string& name = "");
    /**
     * @brief FindMatchWithForceFlag only accept best match and config with ForceMultiConfig
     * @param allConfig
     * @param path
     * @param name
     * @return
     */
    int32_t
    FindMatchWithForceFlag(std::vector<Config*>& allConfig, const std::string& path, const std::string& name = "");

    Config* FindStreamLogTagMatch(const std::string& tag);

    Config* FindDSConfigByCategory(const std::string& dsCtegory);

    Config* FindConfigByName(const std::string& configName);

    // handler must be created by new, because when path timeout, we would call delete on it
    void AddNewHandler(const std::string& path, EventHandler* handler) { mDirEventHandlerMap[path] = handler; }
    /**delete the timeout dir
     *
     * @param dir is the timeout dir
     */
    void RemoveHandler(const std::string& dir, bool delete_flag = true);


    void SetConfigRemoveFlag(bool flag) {
        ReadWriteBarrier();
        mRemoveConfigFlag = flag;
    }

    bool GetConfigRemoveFlag() {
        ReadWriteBarrier();
        return mRemoveConfigFlag;
    }

    void StartUpdateConfig() {
        ReadWriteBarrier();
        mUpdateStat = UPDATE_CONFIG;
    }
    void FinishUpdateConfig() {
        ReadWriteBarrier();
        mUpdateStat = NORMAL;
    }
    bool IsUpdate() {
        ReadWriteBarrier();
        return mUpdateStat != NORMAL;
    }
    bool IsUpdateConfig() {
        ReadWriteBarrier();
        return mUpdateStat == UPDATE_CONFIG;
    }

    void AddHandlerToDelete(EventHandler*);

    Json::Value& GetConfigJson() { return mConfigJson; }

    Json::Value& GetLocalConfigJson() { return mLocalConfigJson; }

    /**
     * @brief Load configs from file and dir in SysConfDir
     *
     * @return true config changed
     * @return false no update
     */
    bool GetLocalConfigUpdate();

    int32_t UpdateConfigJson(const std::string& configJson);

    void RemoveAllConfigs();

    void ClearConfigMatchCache();

    bool NeedReloadMappingConfig() { return mHaveMappingPathConfig && mMappingPathsChanged; }

    void SetMappingPathsChanged() { mMappingPathsChanged = true; }

    virtual bool GetRegionStatus(const std::string& region) = 0;

    void ReloadMappingConfig();

    std::string GetMappingPath(const std::string& id);

    // TODO: Move it to Config.
    bool MatchDirPattern(const Config* config, const std::string& dir);
    void GetRelatedConfigs(const std::string& path, std::vector<Config*>& configs);

    EventHandler* GetSharedHandler() { return mSharedHandler; }

    bool RegisterDirectory(const std::string& source, const std::string& object);

    std::string GetAllProjectsSet();

    bool UpdateContainerPath(DockerContainerPathCmd* cmd);
    bool IsUpdateContainerPaths();
    bool DoUpdateContainerPaths();

    bool UpdateContainerStopped(DockerContainerPathCmd* cmd);
    void GetContainerStoppedEvents(std::vector<Event*>& eventVec);

    void SaveDockerConfig();

    void LoadDockerConfig();

    bool IsEnvConfig() { return mEnvFlag; }

    /**
     * @brief LoadMountPaths
     * @return true if logtail is in container mode and mount paths is changed
     */
    bool LoadMountPaths();

    /**
     * @brief GetMountPaths
     * @return
     */
    DockerMountPaths GetMountPaths();

    void SetCollectionMarkFileExistFlag(bool flag) {
        PTScopedLock lock(mCollectionMarkLock);
        mCollectionMarkFileExistFlag = flag;
    }

    bool GetCollectionMarkFileExistFlag() const {
        PTScopedLock lock(mCollectionMarkLock);
        return mCollectionMarkFileExistFlag;
    }

    virtual void SetStartWorkerStatus(const std::string& result, const std::string& message) = 0;

    virtual std::string CheckPluginFlusher(Json::Value& configJson) = 0;

    virtual Json::Value& CheckPluginProcessor(Json::Value& pluginConfigJson, const Json::Value& rootConfigJson) = 0;

    std::vector<sls_logs::LogTag>& GetFileTags() {
        return mFileTags.getReadBuffer();
    }

    void UpdateFileTags();

    const std::set<std::string>& GetRegionAliuids(const std::string& region);
    void InsertRegionAliuidMap(const std::string& region, const std::string& aliuid);
    void ClearRegionAliuidMap();

    std::unordered_map<std::string, int64_t> GetMServerYamlConfigVersionMap(){ return mServerYamlConfigVersionMap; };
private:
    // no copy
    ConfigManagerBase(const ConfigManagerBase&);
    ConfigManagerBase& operator=(const ConfigManagerBase&);

    void InsertRegion(const std::string& region);

    void ClearRegions();
    /** XXX: path is not registered in this method
     * @param path is the current dir that being registered
     * @depth is the num of sub dir layers that should be registered
     */
    bool RegisterHandlersWithinDepth(const std::string& path, Config* config, int depth);
    bool RegisterDescendants(const std::string& path, Config* config, int withinDepth);
    bool CheckLogType(const std::string& logTypeStr, LogType& logType);
    std::vector<std::string> GetStringVector(const Json::Value& value);
    LogFilterRule* GetFilterFule(const Json::Value& filterKeys, const Json::Value& filterRegs);
    void GetRegexAndKeys(const Json::Value& value, Config* configPtr);
    void GetSensitiveKeys(const Json::Value& value, Config* pConfig);

    /**
     * @brief Load user_local_config.json from sys_conf_dir
     *
     * @return true config changed
     * @return false no update
     */
    bool GetLocalConfigFileUpdate();
    /**
     * @brief Load *.json from user_config.d
     *
     * @return true config changed
     * @return false no update
     */
    bool GetLocalConfigDirUpdate();
    /**
     * @brief Load *.yaml from user_yaml_config.d
     *
     * @return true config changed
     * @return false no update
     */
    bool GetYamlConfigDirUpdate();
    bool CheckYamlDirConfigUpdate(const std::string& configDirPath,
                                  bool isRemote,
                                  std::vector<std::string>& filepathes,
                                  std::unordered_map<std::string, int64_t>& yamlConfigMTimeMap,
                                  bool createIfNotExist);

    /**
     * @brief Load a single data collection config and insert it into mNameConfigMap with name @name.
     *
     * @param name config name
     * @param value config json value
     * @param localFlag
     */
    void LoadSingleUserConfig(const std::string& name, const Json::Value& value, bool localFlag = false);
    bool CheckRegFormat(const std::string& regStr);
    void SendAllMatchAlarm(const std::string& path,
                           const std::string& name,
                           std::vector<Config*>& allConfig,
                           int32_t maxMultiConfigSize);

    void MappingPluginConfig(const Json::Value& configValue, Config* config, Json::Value& pluginJson);

    void InsertProject(const std::string& project);

    void ClearProjects();

    class DoubleBuffer <std::vector<sls_logs::LogTag>>mFileTags;

#ifdef APSARA_UNIT_TEST_MAIN
    void CleanEnviroments();

    friend class EventDispatcherTest;
    friend class SenderUnittest;
    friend class UtilUnittest;
    friend class ConfigUpdatorUnittest;
    friend class ConfigMatchUnittest;
    friend class FuxiSceneUnittest;
    friend class SymlinkInotifyTest;
    friend class LogFilterUnittest;
    friend class FuseFileUnittest;
    friend class MultiServerConfigUpdatorUnitest;
    friend class CreateModifyHandlerUnittest;
#endif
};

class ConfigServiceClientBase {
public:
    ConfigServiceClientBase() = default;
    virtual ~ConfigServiceClientBase() {};
    // initialization client resources such as ak/sk and some variable you need
    virtual void InitClient() = 0;
    // update credential such as ak/sk if your config server needed and it has expire limitation
    virtual bool FlushCredential() = 0;
    // sign http request header if your config server need request with authentication
    virtual void SignHeader(sdk::AsynRequest& request) = 0;
	virtual void SendMetadata() = 0;
	virtual sdk::AsynRequest GenerateHeartBeatRequest(const AppConfig::ConfigServerAddress& configServerAddress, const std::string requestId) = 0;
};

} // namespace logtail
