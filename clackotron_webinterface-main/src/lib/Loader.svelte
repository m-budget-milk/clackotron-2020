<div class="loader-wrapper">
  <div class="loader">
    <div />
    <div />
    <div />
    <div />
    <div />
    <div />
    <div />
    <div />
    <div />
  </div>
</div>

<style>
  .loader-wrapper {
    padding: 100px 0;
    position: relative;
  }

  .loader {
    width: 75px;
    height: 75px;
    position: absolute;
    left: 50%;
    top: 50%;
    margin: -37px 0 0 -37px;
  }

  .loader div {
    width: 15px;
    height: 15px;
    margin: 5px;
    float: left;
    background: rgba(0, 0, 0, 0);
  }

  .loader div:not(:nth-of-type(5)) {
    animation: loaderBlink 2.5s linear infinite;
    -webkit-animation: loaderBlink 2.5s linear infinite;
  }

  .loader div:nth-of-type(5) {
    background: #A20013;
    animation: loaderSpin 2.5s linear infinite;
    -webkit-animation: loaderSpin 2.5s linear infinite;
  }

  @keyframes loaderBlink {
    0% {
      background: rgba(0, 0, 0, 0.1);
    }
    45% {
      background: rgba(0, 0, 0, 0);
    }
    100% {
      background: rgba(0, 0, 0, 0.1);
    }
  }
  @-webkit-keyframes loaderBlink {
    0% {
      background: rgba(0, 0, 0, 0.1);
    }
    45% {
      background: rgba(0, 0, 0, 0);
    }
    100% {
      background: rgba(0, 0, 0, 0.1);
    }
  }

  @keyframes loaderSpin {
    0% {
      -webkit-border-radius: 0px;
      -webkit-transform: rotate(0deg);
    }
    45% {
      -webkit-border-radius: 6px;
    }
    100% {
      -webkit-border-radius: 0px;
      -webkit-transform: rotate(360deg);
    }
  }
  @-webkit-keyframes loaderSpin {
    0% {
      -webkit-border-radius: 0px;
      -webkit-transform: rotate(0deg);
    }
    45% {
      -webkit-border-radius: 6px;
    }
    100% {
      -webkit-border-radius: 0px;
      -webkit-transform: rotate(360deg);
    }
  }
</style>
