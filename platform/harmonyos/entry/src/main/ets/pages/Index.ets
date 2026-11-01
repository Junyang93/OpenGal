import nativeRender from 'libentry.so';

@Entry
@Component
struct Index {
  private xComponentId: string = 'gal_xcomponent';

  build() {
    Column() {
      XComponent({
        id: this.xComponentId,
        type: XComponentType.SURFACE,
        libraryname: 'entry'
      })
        .onLoad((context?: object) => {
          // 注册 XComponent 回调与资源管理器(游戏资源在 resources/rawfile 下)
          nativeRender.register(context, getContext(this).resourceManager);
        })
        .onDestroy(() => {
        })
        .width('100%')
        .height('100%')
    }
    .width('100%')
    .height('100%')
    .backgroundColor(Color.Black)
  }
}
